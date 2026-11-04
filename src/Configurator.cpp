// Copyright (c) 2009-2024 LG Electronics, Inc.
// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "BusClient.h"
#include "Configurator.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <glib.h>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Where the stamp for a configuration file lives: its path, with each '/' made a '_', in the
// stamp cache.
std::string StampPath(const std::string& confFile)
{
	std::string name = confFile;
	std::ranges::replace(name, '/', '_');
	return kConfCacheDir + name;
}

// What a stamp records: the SHA-256 of a configuration's contents, in hex.
std::string ContentChecksum(const std::string& contents)
{
	gchar* sum = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
			reinterpret_cast<const guchar*>(contents.data()), contents.size());
	std::string out(sum ? sum : "");
	g_free(sum);
	return out;
}

} // namespace

ConfiguratorCallback::ConfiguratorCallback(Configurator* configurator, const std::string& filePath)
	: m_slot(this, &ConfiguratorCallback::ResponseWrapper),
	  m_config(filePath),
	  m_handler(configurator)
{
	assert(m_handler.get() != nullptr);
}

ConfiguratorCallback::~ConfiguratorCallback() = default;

MojErr ConfiguratorCallback::DelegateResponse(MojObject& response, MojErr err)
{
	if (m_delegateInvoked)
		return MojErrAccessDenied;
	m_delegateInvoked = true;
	m_defaultCacheBehaviourUsed = false;

	return m_handler->BusResponseAsync(m_config, response, err, &m_defaultCacheBehaviourUsed);
}

void ConfiguratorCallback::MarkConfigured()
{
	assert(!m_unconfigure);
	m_configure = true;
}

void ConfiguratorCallback::UnmarkConfigured()
{
	assert(!m_configure);
	m_unconfigure = true;
}

MojErr ConfiguratorCallback::ResponseWrapper(MojObject& response, MojErr err)
{
	MojErr result = MojErrNone;
	try {
		m_slot.cancel();
		result = Response(response, err);
	} catch (const std::exception& e) {
		MojErrThrowMsg(MojErrInternal, "%s", e.what());
	} catch (...) {
		MojErrThrowMsg(MojErrInternal, "Uncaught exception in Configurator::BusResponse!");
	}

	if (!m_delegateInvoked)
		result = DelegateResponse(response, result == MojErrNone ? err : result);

	if (!m_defaultCacheBehaviourUsed) {
		if (m_unconfigure) {
			LOG_DEBUG("Unmarking %s as configured", m_config.c_str());
			m_handler->UnmarkConfigured(m_config);
		} else if (m_configure) {
			LOG_DEBUG("Marking %s as configured", m_config.c_str());
			m_handler->MarkConfigured(m_config);
		}
	}
	return result;
}

MojErr DefaultConfiguratorCallback::Response(MojObject&, MojErr)
{
	// no action - do the default response
	return MojErrNone;
}

Configurator::ConfigCollection Configurator::m_configureOk;
Configurator::ConfigCollection Configurator::m_configureFailed;

void Configurator::ResetConfigStats()
{
	m_configureOk.clear();
	m_configureFailed.clear();
}

const Configurator::ConfigCollection& Configurator::ConfigureOk()
{
	return m_configureOk;
}

const Configurator::ConfigCollection& Configurator::ConfigureFailure()
{
	return m_configureFailed;
}

Configurator::Configurator(const std::string& id, ConfigType confType, RunType type, BusClient& busClient, const std::string& configDirectory)
	: m_busClient(busClient),
	  m_id(id),
	  m_confType(confType),
	  m_currentType(type),
	  m_configDir(configDirectory)
{
	InitCacheDir();
}

Configurator::~Configurator()
{
	LOG_DEBUG("Destroying configurator %p", static_cast<void*>(this));
}

ConfiguratorCallback* Configurator::CreateCallback(const std::string& filePath)
{
	return new DefaultConfiguratorCallback(this, filePath);
}

void Configurator::InitCacheDir() const
{
	MojMkDir(kCacheDir, kCacheDirPerms);
	MojMkDir(kConfCacheDir, kCacheStampPerm);
}

// The checksum a stamp for confFile is to record: that of the contents Run() sent, not of
// the file as it is now. Were it read again here, a file changed while the service was
// answering would be stamped with contents that were never configured, and skipped from
// then on.
std::string Configurator::ProcessedChecksum(const std::string& confFile) const
{
	const auto i = m_processedChecksums.find(confFile);
	if (i != m_processedChecksums.end())
		return i->second;
	// marked without having been through Run() in this configurator
	return ContentChecksum(ReadFile(confFile));
}

bool Configurator::IsAlreadyConfigured(const std::string& confFile) const
{
	if (!CanCacheConfiguratorStatus(confFile)) {
		LOG_DEBUG("Configurator ignores caching - returning false");
		return false;
	}

	// A file counts as configured while its contents are what the stamp records. Comparing
	// mtimes is not enough: a reproducible image gives every file the same mtime, so a file an
	// update changed never looked newer than its stamp and was skipped for good. A stamp that
	// records no checksum (one from before this check) matches nothing, so its file is
	// configured once more.
	const std::string stamp = StampPath(confFile);
	MojStatT confInfo;
	if (MojErrNone != MojStat(confFile.c_str(), &confInfo))
		return false;

	const std::string recorded = ReadFile(stamp);
	if (recorded.empty())
		return false;

	LOG_DEBUG("%s may already be configured - %s exists", confFile.c_str(), stamp.c_str());
	return recorded == ContentChecksum(ReadFile(confFile));
}

void Configurator::MarkConfigured(const std::string& confFile) const
{
	if (!CanCacheConfiguratorStatus(confFile))
		return;

	LOG_DEBUG("Attempting to mark '%s' as configured", confFile.c_str());

	MojStatT confFileInfo;
	const MojErr err = MojStat(confFile.c_str(), &confFileInfo);

	// The stamp gets the file's mtime plus a second; atime is "now", as Linux does not
	// honour UTIME_OMIT here.
	struct timespec inherited[2];
	struct timespec* times = nullptr;
	if (err == MojErrNone) {
		inherited[0].tv_nsec = UTIME_NOW;
		inherited[0].tv_sec = 0;
		inherited[1].tv_nsec = confFileInfo.st_mtim.tv_nsec;
		inherited[1].tv_sec = confFileInfo.st_mtim.tv_sec + 1;
		times = inherited;
	} else {
		LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 1,
			PMLOGKS("error", strerror(errno)),
			"Using current time as timestamp - couldn't get timestamp of conf file (%s)", strerror(errno));
	}

	// O_NOFOLLOW: a symlink planted in the cache must not redirect the write.
	const std::string stamp = StampPath(confFile);
	const int stampFd = open(stamp.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_NOATIME | O_NOFOLLOW | O_CLOEXEC, kCacheStampPerm);
	if (stampFd == -1) {
		LOG_ERROR(MSGID_CONFIGURATOR_ERROR, 2,
			PMLOGKS("file", confFile.c_str()),
			PMLOGKS("error", strerror(errno)),
			"Failed to mark %s as configured: %s", confFile.c_str(), strerror(errno));
		return;
	}

	// A stamp left without its checksum only makes the file configure again next time.
	const std::string sum = ProcessedChecksum(confFile);
	const ssize_t written = sum.empty() ? -1 : write(stampFd, sum.data(), sum.size());
	if (written < 0 || static_cast<size_t>(written) != sum.size())
		LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 1,
			PMLOGKS("file", confFile.c_str()),
			"Could not record the checksum of %s; it is configured again next time", confFile.c_str());

	if (-1 == futimens(stampFd, times)) {
		(void)unlink(stamp.c_str());
		LOG_ERROR(MSGID_CONFIGURATOR_ERROR, 2,
			PMLOGKS("file", confFile.c_str()),
			PMLOGKS("error", strerror(errno)),
			"Failed to create configured stamp for %s (timestamp change failed: %s)", confFile.c_str(), strerror(errno));
	} else {
		LOG_DEBUG("'%s' marked as configured (stamp '%s' created)", confFile.c_str(), stamp.c_str());
	}

	(void)close(stampFd);
}

void Configurator::UnmarkConfigured(const std::string& confFile) const
{
	if (!CanCacheConfiguratorStatus(confFile))
		return;

	const std::string stamp = StampPath(confFile);
	if (unlink(stamp.c_str()) == 0) {
		LOG_DEBUG("removed configured stamp for '%s'", confFile.c_str());
	} else {
		LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 2,
			PMLOGKS("file", confFile.c_str()),
			PMLOGKS("stamp", stamp.c_str()),
			"failed to remove configured stamp for '%s' ('%s')", confFile.c_str(), stamp.c_str());
	}
}

const std::string& Configurator::ParentId(const std::string& filePath) const
{
	const auto i = m_parentDirMap.find(filePath);
	if (i == m_parentDirMap.end() || i->second.empty())
		return m_id;
	return i->second;
}

bool Configurator::CanCacheConfiguratorStatus(const std::string&) const
{
	LOG_TRACE("Entering function %s", __FUNCTION__);
	return true;
}

bool Configurator::Run()
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	if (!m_scanned) {
		const bool folderFound = GetConfigFiles("", m_configDir);
		if (m_configs.empty() && folderFound) // a missing folder has been logged already
			LOG_DEBUG("No configurations found in %s", m_configDir.c_str());
		m_emptyConfigurator = m_configs.empty();
		m_scanned = true;
	}

	// Send the next file's request. One that fails or needs no request is passed over for
	// the one after it, in a loop: a directory of failing files does not grow the stack.
	while (!m_configs.empty()) {
		const std::string filePath = m_configs.back();
		m_configs.pop_back();
		m_pendingConfigs.push_back(filePath);
		const std::string config = ReadFile(filePath);
		m_processedChecksums[filePath] = ContentChecksum(config);

		LOG_DEBUG("%s :: Configuring '%s'", ConfiguratorName(), filePath.c_str());

		MojErr err = MojErrNone;
		switch (m_currentType) {
		case Configure:
		case Reconfigure:
			err = ProcessConfig(filePath, config);
			break;
		case RemoveConfiguration:
			err = ProcessConfigRemoval(filePath, config);
			break;
		}

		if (!err)
			return m_configs.empty();

		if (MojErrInProgress == err) {
			m_configureOk.push_back(filePath);
			LOG_DEBUG("Skipping config file: %s", filePath.c_str());
		} else {
			MojString errorMsg;
			MojErrToString(err, errorMsg);
			LOG_ERROR(MSGID_CONFIGURATOR_ERROR, 2,
					PMLOGKS("config", filePath.c_str()),
					PMLOGKS("error", errorMsg.data()),
					"Failed to process config: %s (error: %s)", filePath.c_str(), errorMsg.data());
			// Skip this file and keep going!
			m_configureFailed.push_back(filePath);
		}
		m_pendingConfigs.pop_back();
	}

	if (m_pendingConfigs.empty() && !m_completed) {
		if (!m_emptyConfigurator)
			LOG_DEBUG("%s :: No more configurations", ConfiguratorName());
		Complete();
	} else {
		LOG_DEBUG("%s :: %zu configurations pending, m_completed = %d", ConfiguratorName(), m_pendingConfigs.size(), m_completed);
	}
	// nothing to do - already sent out all the requests
	// just waiting for responses from services
	return true;
}

MojErr Configurator::ProcessConfig(const std::string& filePath, const std::string& json)
{
	MojObject parsed;
	MojErr err = parsed.fromJson(json.c_str());
	MojErrCheck(err);

	return ProcessConfig(filePath, parsed);
}

MojErr Configurator::ProcessConfigRemoval(const std::string& filePath, const std::string& json)
{
	MojObject parsed;
	MojErr err = parsed.fromJson(json.c_str());
	MojErrCheck(err);

	return ProcessConfigRemoval(filePath, parsed);
}

// returns whether the folder exists
bool Configurator::GetConfigFiles(const std::string& parent, const std::string& directory)
{
	VisitedDirs visited;
	return GetConfigFiles(parent, directory, visited);
}

bool Configurator::GetConfigFiles(const std::string& parent, const std::string& directory, VisitedDirs& visited)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	DIR* dp = opendir(directory.c_str());
	if (dp == nullptr) {
		LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 2,
				PMLOGKS("directory", directory.c_str()),
				PMLOGKS("parent", parent.c_str()),
				"Failed to open directory: %s, under %s", directory.c_str(), parent.c_str());
		return false;
	}

	struct stat dirStat;
	if (fstat(dirfd(dp), &dirStat) == 0 && !visited.emplace(dirStat.st_dev, dirStat.st_ino).second) {
		LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 1,
				PMLOGKS("directory", directory.c_str()),
				"Skipping %s: already scanned (a symlink loop?)", directory.c_str());
		(void)closedir(dp);
		return true;
	}

	LOG_DEBUG("Reading config files in '%s' under '%s'", directory.c_str(), parent.c_str());

	while (const struct dirent* dirp = readdir(dp)) {
		const std::string filename = dirp->d_name;
		if (filename == "." || filename == "..")
			continue;

		// stat, not lstat: a configuration may be a symlink to its file.
		const std::string filePath = directory + "/" + filename;
		struct stat statBuf;
		if (stat(filePath.c_str(), &statBuf) != 0) {
			// a dangling symlink, or a file gone since readdir: the rest are still read
			LOG_ERROR(MSGID_CONFIGURATOR_ERROR, 0, "Failed to get file information on: %s", filePath.c_str());
			continue;
		}

		if (S_ISDIR(statBuf.st_mode)) {
			(void)GetConfigFiles(filename, filePath, visited);
			continue;
		}

		// Only regular files: reading a FIFO or a device would block the configurator.
		if (!S_ISREG(statBuf.st_mode)) {
			LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 1,
					PMLOGKS("file", filePath.c_str()),
					"Skipping %s: not a regular file", filePath.c_str());
			continue;
		}

		if (!parent.empty())
			m_parentDirMap[filePath] = parent;
		if (m_currentType == Configure && IsAlreadyConfigured(filePath)) {
			LOG_DEBUG("Skipping configuration '%s' because it has already run (cache stamp in %s exists)", filePath.c_str(), kConfCacheDir);
		} else {
			LOG_DEBUG("Found configuration '%s'", filePath.c_str());
			m_configs.push_back(filePath);
		}
	}
	(void)closedir(dp);
	return true;
}

std::string Configurator::ReadFile(const std::string& filePath) const
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	std::ifstream file(filePath, std::ios::binary);
	if (!file.good())
		return {};

	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void Configurator::Complete()
{
	m_busClient.ConfiguratorComplete(this);
	m_completed = true;
}

MojErr Configurator::BusResponseAsync(const std::string& config, MojObject& response, MojErr err, bool* cacheConfigured)
{
	LOG_TRACE("Entering function %s", __FUNCTION__);

	try {
		// remove the config from the list
		const auto i = std::ranges::find(m_pendingConfigs, config);
		if (i == m_pendingConfigs.end()) {
			LOG_WARNING(MSGID_CONFIGURATOR_WARNING, 1,
					PMLOGKS("for", config.c_str()),
					"Response for %s but not in pending list", config.c_str());
		} else {
			LOG_DEBUG("Response for %s - removing from pending list", config.c_str());
			(void)m_pendingConfigs.erase(i);
		}

		bool success = true;
		(void)response.get("returnValue", success);

		if (err || !success) {
			m_configureFailed.push_back(config);

			MojString json;
			MojErrCheck(response.toJson(json));
			LOG_ERROR(MSGID_CONFIGURATOR_ERROR, 2,
					PMLOGKS("config", config.c_str()),
					PMLOGKFV("error", "%d", err),
					"%s: %s (MojErr: %i)", config.c_str(), json.data(), err);
		} else {
			m_configureOk.push_back(config);

			*cacheConfigured = true;
			if (m_currentType != RemoveConfiguration)
				MarkConfigured(config);
			else
				UnmarkConfigured(config);
		}

		// do the next config
		(void)Run();
	} catch (const std::exception& e) {
		MojErrThrowMsg(MojErrInternal, "%s", e.what());
	} catch (...) {
		MojErrThrowMsg(MojErrInternal, "Uncaught exception in Configurator::BusResponse!");
	}
	return MojErrNone;
}
