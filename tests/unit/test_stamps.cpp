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
// Whether a configuration counts as configured: it does while its contents are what its stamp
// records, whatever the files' mtimes say. A reproducible image gives every file the same
// mtime, so the old mtime check skipped a file an update had changed.

#include "fixture.h"

using namespace testutil;
using A = ConfiguratorTestAccess;

namespace {

const char kContents[] = "[{\"caller\":\"com.palm.*\"}]";
// sha256sum of kContents, worked out apart from the code under test.
const char kContentsSha256[] = "f352ed8a90358141cb7d5344644ab14d8a5def619da518b7901a8929328b3188";

class Stamps : public ConfigDir {
protected:
	void SetUp() override
	{
		ConfigDir::SetUp();
		conf = File("com.palm.immessage.libpurple", kContents);
		SetImageTime(conf);
		configurator.reset(new RecordingConfigurator(bus, dir));
	}

	void TearDown() override
	{
		configurator.reset();
		(void)unlink(Stamp(conf).c_str()); // also when a test removed conf itself
		ConfigDir::TearDown();
	}

	bool IsConfigured() { return A::IsAlreadyConfigured(*configurator, conf); }
	void Mark() { A::MarkConfigured(*configurator, conf); }

	MojRefCountedPtr<RecordingConfigurator> configurator;
	std::string conf;
};

TEST_F(Stamps, NotConfiguredWithoutAStamp)
{
	EXPECT_FALSE(IsConfigured());
}

TEST_F(Stamps, ConfiguredOnceMarked)
{
	Mark();
	EXPECT_TRUE(IsConfigured());
}

TEST_F(Stamps, TheStampRecordsTheSha256OfTheContents)
{
	Mark();
	EXPECT_EQ(kContentsSha256, Read(Stamp(conf)));
}

TEST_F(Stamps, ConfiguredAgainWhenTheContentsChangeButTheMtimeDoesNot)
{
	Mark();
	// An image update: new contents, the same fixed mtime.
	Write(conf, "[{\"caller\":\"com.palm.*\"},{\"caller\":\"org.webosports.service.messaging\"}]");
	SetImageTime(conf);
	EXPECT_FALSE(IsConfigured());

	Mark();
	EXPECT_TRUE(IsConfigured());
}

TEST_F(Stamps, ConfiguredAgainWhenTheStampRecordsNoChecksum)
{
	// A stamp from before the checksum: empty, and newer than the file.
	Write(Stamp(conf), "");
	EXPECT_FALSE(IsConfigured());
}

TEST_F(Stamps, ALongerOldStampIsOverwrittenWhole)
{
	Write(Stamp(conf), std::string(200, 'x'));
	Mark();
	EXPECT_TRUE(IsConfigured());
	EXPECT_EQ(64u, Read(Stamp(conf)).size());
}

TEST_F(Stamps, NotConfiguredWhenTheFileIsGone)
{
	Mark();
	(void)unlink(conf.c_str());
	EXPECT_FALSE(IsConfigured());
}

TEST_F(Stamps, UnmarkingRemovesTheStamp)
{
	Mark();
	A::UnmarkConfigured(*configurator, conf);
	EXPECT_NE(0, access(Stamp(conf).c_str(), F_OK));
	EXPECT_FALSE(IsConfigured());
}

// The stamp is for what was sent. A file changed while its service was answering has to be
// configured again, not stamped with contents that never reached the service.
TEST_F(Stamps, TheStampIsForTheContentsThatWereSent)
{
	(void)A::Run(*configurator); // sends conf, waits for the answer
	ASSERT_EQ(1u, configurator->processed.size());

	Write(conf, "[{\"caller\":\"changed.while.the.service.answered\"}]");
	SetImageTime(conf);
	MojObject ok = Json("{\"returnValue\":true}");
	EXPECT_EQ(MojErrNone, A::Respond(*configurator, conf, ok, MojErrNone));

	EXPECT_EQ(kContentsSha256, Read(Stamp(conf)));
	EXPECT_FALSE(IsConfigured());
}

TEST_F(Stamps, AnAnswerWithAnErrorLeavesNoStamp)
{
	(void)A::Run(*configurator);
	MojObject failed = Json("{\"returnValue\":false}");
	EXPECT_EQ(MojErrNone, A::Respond(*configurator, conf, failed, MojErrNone));
	EXPECT_FALSE(IsConfigured());
	ASSERT_EQ(1u, Configurator::ConfigureFailure().size());
	EXPECT_EQ(conf, Configurator::ConfigureFailure()[0]);
}

// A symlink planted where the stamp goes must not have the stamp written through it.
TEST_F(Stamps, AStampIsNotWrittenThroughASymlink)
{
	const std::string target = dir + "/elsewhere";
	Write(target, "untouched");
	ASSERT_EQ(0, symlink(target.c_str(), Stamp(conf).c_str()));
	Mark();
	EXPECT_EQ("untouched", Read(target));
	EXPECT_FALSE(IsConfigured());
}

TEST_F(Stamps, CheckingAndMarkingLeaveNoDescriptorOpen)
{
	const int before = OpenDescriptors();
	ASSERT_GT(before, 0);
	for (int i = 0; i < 10; ++i) {
		Mark();
		(void)IsConfigured();
	}
	EXPECT_EQ(before, OpenDescriptors());
}

} // namespace
