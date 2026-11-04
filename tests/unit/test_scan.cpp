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
// Which files a scan of a configuration directory takes: every regular file, under any
// depth of subdirectories, each once, past entries it cannot use.

#include "fixture.h"

#include <algorithm>

using namespace testutil;
using A = ConfiguratorTestAccess;

namespace {

class Scan : public ConfigDir {
protected:
	// Everything the configurator is handed, sorted. Each request is answered with success;
	// an answer makes the configurator send the next one.
	std::vector<std::string> Processed(Configurator::RunType type = Configurator::Configure)
	{
		MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir, type));
		(void)A::Run(*c);
		for (size_t answered = 0; answered < c->processed.size(); ++answered) {
			MojObject ok = Json("{\"returnValue\":true}");
			(void)A::Respond(*c, c->processed[answered], ok, MojErrNone);
		}
		EXPECT_TRUE(A::Pending(*c).empty());
		std::vector<std::string> out = c->processed;
		std::ranges::sort(out);
		return out;
	}
};

TEST_F(Scan, TakesEveryFileUnderAnyDepth)
{
	const std::string a = File("a.json");
	const std::string b = File("com.example.app/b.json");
	const std::string c = File("com.example.app/deeper/c.json");
	EXPECT_EQ((std::vector<std::string>{a, b, c}), Processed());
}

TEST_F(Scan, AFileIsOwnedByTheDirectoryItIsIn)
{
	const std::string b = File("com.example.app/b.json");
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir));
	(void)A::Run(*c);
	EXPECT_EQ("com.example.app", A::ParentId(*c, b));
}

// It ended the scan of its directory: every file after it was left out.
TEST_F(Scan, ADanglingSymlinkDoesNotStopTheScan)
{
	std::vector<std::string> files;
	for (int i = 0; i < 20; ++i)
		files.push_back(File("f" + std::to_string(i) + ".json"));
	ASSERT_EQ(0, symlink((dir + "/nowhere").c_str(), (dir + "/dangling.json").c_str()));
	std::ranges::sort(files);
	EXPECT_EQ(files, Processed());
}

// It sent the scan round until the path was too long to open, taking every file in again on
// each turn.
TEST_F(Scan, ASymlinkLoopIsScannedOnce)
{
	const std::string a = File("sub/a.json");
	ASSERT_EQ(0, symlink("..", (dir + "/sub/up").c_str()));
	EXPECT_EQ((std::vector<std::string>{a}), Processed());
}

// Reading a FIFO blocks until something writes to it.
TEST_F(Scan, OnlyRegularFilesAreTaken)
{
	const std::string a = File("a.json");
	ASSERT_EQ(0, mkfifo((dir + "/fifo.json").c_str(), 0600));
	EXPECT_EQ((std::vector<std::string>{a}), Processed());
}

TEST_F(Scan, ASymlinkToAFileIsTaken)
{
	const std::string a = File("a.json");
	const std::string link = dir + "/link.json";
	ASSERT_EQ(0, symlink(a.c_str(), link.c_str()));
	EXPECT_EQ((std::vector<std::string>{a, link}), Processed());
}

TEST_F(Scan, AConfiguredFileIsSkippedUnlessReconfiguring)
{
	const std::string a = File("a.json");
	EXPECT_EQ((std::vector<std::string>{a}), Processed());
	EXPECT_TRUE(Processed().empty());
	EXPECT_EQ((std::vector<std::string>{a}), Processed(Configurator::Reconfigure));
}

TEST_F(Scan, AMissingDirectoryIsAnEmptyConfigurator)
{
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir + "/missing"));
	EXPECT_TRUE(A::Run(*c));
	EXPECT_TRUE(c->processed.empty());
}

} // namespace
