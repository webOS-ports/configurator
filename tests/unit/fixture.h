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
//
// What the suites share: a configuration directory of their own under the scratch stamp
// cache (Configurator.h is generated with CONFIGURATOR_TEST_LOCALSTATEDIR), a configurator
// that records what it is handed instead of calling a service, and a message that stands in
// for a bus call. Nothing here goes on the bus: a BusClient is only constructed, not opened.

#ifndef CONFIGURATOR_TESTS_FIXTURE_H_
#define CONFIGURATOR_TESTS_FIXTURE_H_

#include <gtest/gtest.h>

#include "BusClient.h"
#include "Configurator.h"
#include "core/MojObjectBuilder.h"
#include "core/MojServiceMessage.h"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Declared a friend of Configurator and BusClient.
struct ConfiguratorTestAccess {
	static bool IsAlreadyConfigured(const Configurator& c, const std::string& conf) { return c.IsAlreadyConfigured(conf); }
	static void MarkConfigured(const Configurator& c, const std::string& conf) { c.MarkConfigured(conf); }
	static void UnmarkConfigured(const Configurator& c, const std::string& conf) { c.UnmarkConfigured(conf); }
	static bool Run(Configurator& c) { return c.Run(); }
	static MojErr Respond(Configurator& c, const std::string& conf, MojObject& response, MojErr err)
	{
		bool cached = false;
		return c.BusResponseAsync(conf, response, err, &cached);
	}
	static const Configurator::ConfigCollection& Pending(const Configurator& c) { return c.m_pendingConfigs; }
	static const std::string& ParentId(const Configurator& c, const std::string& file) { return c.ParentId(file); }

	using PackageRequest = BusClient::PackageRequest;
	static MojErr Parse(const MojObject& payload, bool withTypes, std::vector<PackageRequest>& out)
	{
		return BusClient::ParsePackageRequests(payload, withTypes, out);
	}
	static bool IsApplication(const PackageRequest& r) { return r.type == BusClient::Application; }
	static bool IsThirdParty(const PackageRequest& r) { return r.location == BusClient::ThirdParty; }
	static MojServiceMessage* KeptMessage(const BusClient& bus) { return bus.m_msg.get(); }
	static size_t Configurators(const BusClient& bus) { return bus.m_configurators.size(); }
	static MojLunaService& Service(BusClient& bus) { return bus.m_service; }

	// The bus methods as the bus would call them: through MojService's callback type.
	enum class Method { Scan, Rescan, Unconfigure, Run };
	static MojErr Call(BusClient& bus, Method method, MojServiceMessage* msg, const MojObject& payload)
	{
		MojRefCountedPtr<BusClient::BusMethods> methods(new BusClient::BusMethods(bus));
		using Callback = MojService::CategoryHandler::Callback;
		Callback cb = nullptr;
		switch (method) {
		case Method::Scan: cb = static_cast<Callback>(&BusClient::BusMethods::Scan); break;
		case Method::Rescan: cb = static_cast<Callback>(&BusClient::BusMethods::Rescan); break;
		case Method::Unconfigure: cb = static_cast<Callback>(&BusClient::BusMethods::Unconfigure); break;
		case Method::Run: cb = static_cast<Callback>(&BusClient::BusMethods::Run); break;
		}
		MojService::CategoryHandler* handler = methods.get();
		return (handler->*cb)(msg, payload);
	}
};

namespace testutil {

// A bus call that is never sent: it counts the replies made to it.
class FakeMessage : public MojServiceMessage {
public:
	explicit FakeMessage(MojService* service) : MojServiceMessage(service, nullptr) {}

	MojObjectVisitor& writer() override { return m_writer; }
	const MojChar* appId() const override { return nullptr; }
	const MojChar* category() const override { return "/"; }
	const MojChar* method() const override { return "test"; }
	const MojChar* senderAddress() const override { return "test"; }
	const MojChar* senderId() const override { return "test"; }
	const MojChar* senderExePath() const override { return "/test"; }
	const MojChar* senderTrustLevel() const override { return "dev"; }
	MojErr payload(MojObjectVisitor&) const override { return MojErrNone; }
	MojErr payload(MojObject&) const override { return MojErrNone; }
	Token token() const override { return 0; }
	bool hasData() const override { return false; }
	const MojChar* queue() const override { return "test"; }

	int replies = 0;

protected:
	MojErr replyImpl() override { ++replies; return MojErrNone; }
	MojErr handleCancel() override { return MojErrNone; }

private:
	MojObjectBuilder m_writer;
};

inline MojObject Json(const char* text)
{
	MojObject o;
	EXPECT_EQ(MojErrNone, o.fromJson(text)) << text;
	return o;
}

inline void Write(const std::string& path, const std::string& text)
{
	std::ofstream f(path.c_str(), std::ios::trunc | std::ios::binary);
	f << text;
}

inline std::string Read(const std::string& path)
{
	std::ifstream f(path.c_str(), std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline std::string Stamp(const std::string& conf)
{
	std::string s = conf;
	for (auto& c : s) {
		if (c == '/')
			c = '_';
	}
	return std::string(kConfCacheDir) + s;
}

// mkdir -p
inline void MakeDirs(const std::string& path)
{
	for (std::string::size_type i = 1; i <= path.size(); ++i) {
		if (i == path.size() || path[i] == '/')
			(void)mkdir(path.substr(0, i).c_str(), 0755);
	}
}

// rm -r, without following symlinks; also removes the stamps of what it removes.
inline void RemoveTree(const std::string& path)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0)
		return;
	if (S_ISDIR(st.st_mode)) {
		if (DIR* d = opendir(path.c_str())) {
			while (const struct dirent* e = readdir(d)) {
				const std::string name = e->d_name;
				if (name != "." && name != "..")
					RemoveTree(path + "/" + name);
			}
			(void)closedir(d);
		}
		(void)rmdir(path.c_str());
	} else {
		(void)unlink(Stamp(path).c_str());
		(void)unlink(path.c_str());
	}
}

inline int OpenDescriptors()
{
	int n = 0;
	DIR* d = opendir("/proc/self/fd");
	if (!d)
		return -1;
	while (readdir(d))
		++n;
	(void)closedir(d);
	return n;
}

// The fixed mtime a reproducible image gives every file (2011-04-06 01:00 +0200).
inline void SetImageTime(const std::string& path)
{
	struct timespec t[2];
	t[0].tv_sec = t[1].tv_sec = 1302044400;
	t[0].tv_nsec = t[1].tv_nsec = 0;
	ASSERT_EQ(0, utimensat(AT_FDCWD, path.c_str(), t, 0)) << path << ": " << errno;
}

// A configurator that records each configuration it is handed instead of sending it, and
// answers with whatever result a test asks for.
class RecordingConfigurator : public Configurator {
public:
	RecordingConfigurator(BusClient& bus, const std::string& dir, RunType type = Configure)
		: Configurator("", ConfigService, type, bus, dir) {}

	const char* ConfiguratorName() const override { return "RecordingConfigurator"; }
	const char* ServiceName() const override { return "test"; }

	MojErr ProcessConfig(const std::string& filePath, MojObject&) override
	{
		processed.push_back(filePath);
		return result;
	}
	MojErr ProcessConfigRemoval(const std::string& filePath, MojObject&) override
	{
		removed.push_back(filePath);
		return result;
	}

	MojErr result = MojErrNone;
	std::vector<std::string> processed;
	std::vector<std::string> removed;
};

// A configuration directory of its own under the scratch cache, removed again afterwards
// with every stamp it left.
class ConfigDir : public ::testing::Test {
protected:
	void SetUp() override
	{
		MakeDirs(kConfCacheDir);
		const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
		dir = std::string(kCacheDir) + "conf-" + std::to_string(getpid()) + "-" + info->name();
		RemoveTree(dir);
		ASSERT_EQ(0, mkdir(dir.c_str(), 0755)) << dir << ": " << errno;
		Configurator::ResetConfigStats();
	}

	void TearDown() override
	{
		RemoveTree(dir);
		Configurator::ResetConfigStats();
	}

	std::string File(const std::string& name, const std::string& text = "{}")
	{
		const std::string path = dir + "/" + name;
		MakeDirs(path.substr(0, path.rfind('/')));
		Write(path, text);
		return path;
	}

	// Only constructed: nothing here registers on the bus.
	BusClient bus;
	std::string dir;
};

} // namespace testutil

#endif // CONFIGURATOR_TESTS_FIXTURE_H_
