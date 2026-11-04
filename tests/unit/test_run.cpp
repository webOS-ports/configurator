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
// Configurator::Run(): a configuration that fails, or needs no request, is counted and passed
// over for the next one, however many of them there are.

#include "fixture.h"

#include <algorithm>
#include <pthread.h>

using namespace testutil;
using A = ConfiguratorTestAccess;

namespace {

class RunConfigs : public ConfigDir {};

std::vector<std::string> Sorted(std::vector<std::string> v)
{
	std::ranges::sort(v);
	return v;
}

TEST_F(RunConfigs, AFailureIsCountedByPathAndTheRestStillRun)
{
	const std::vector<std::string> files = {File("a.json"), File("b.json"), File("c.json")};
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir));
	c->result = MojErrInvalidArg;
	EXPECT_TRUE(A::Run(*c));
	EXPECT_EQ(files, Sorted(c->processed));
	EXPECT_EQ(files, Sorted(Configurator::ConfigureFailure()));
	EXPECT_TRUE(Configurator::ConfigureOk().empty());
	EXPECT_TRUE(A::Pending(*c).empty());
}

// It was counted by the file's contents.
TEST_F(RunConfigs, OneThatNeedsNoRequestIsCountedByPath)
{
	const std::string a = File("a.json", "{\"some\":\"contents\"}");
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir));
	c->result = MojErrInProgress;
	EXPECT_TRUE(A::Run(*c));
	EXPECT_EQ(std::vector<std::string>{a}, Configurator::ConfigureOk());
	EXPECT_TRUE(Configurator::ConfigureFailure().empty());
}

TEST_F(RunConfigs, OneThatDoesNotParseIsAFailure)
{
	const std::string a = File("a.json", "{not json");
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir));
	EXPECT_TRUE(A::Run(*c));
	EXPECT_TRUE(c->processed.empty());
	EXPECT_EQ(std::vector<std::string>{a}, Configurator::ConfigureFailure());
}

TEST_F(RunConfigs, EachAnswerSendsTheNext)
{
	const std::vector<std::string> files = {File("a.json"), File("b.json")};
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir));
	EXPECT_FALSE(A::Run(*c));
	ASSERT_EQ(1u, c->processed.size());
	EXPECT_EQ(1u, A::Pending(*c).size());

	MojObject ok = Json("{\"returnValue\":true}");
	(void)A::Respond(*c, c->processed[0], ok, MojErrNone);
	ASSERT_EQ(2u, c->processed.size());
	ok = Json("{\"returnValue\":true}");
	(void)A::Respond(*c, c->processed[1], ok, MojErrNone);
	EXPECT_TRUE(A::Pending(*c).empty());
	EXPECT_EQ(files, Sorted(Configurator::ConfigureOk()));
}

TEST_F(RunConfigs, RemovalHandsEachFileToTheRemoval)
{
	const std::string a = File("a.json");
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir, Configurator::RemoveConfiguration));
	c->result = MojErrInProgress;
	EXPECT_TRUE(A::Run(*c));
	EXPECT_TRUE(c->processed.empty());
	EXPECT_EQ(std::vector<std::string>{a}, c->removed);
}

struct StackRun {
	RecordingConfigurator* configurator;
	bool done = false;
};

void* RunOnSmallStack(void* arg)
{
	auto* run = static_cast<StackRun*>(arg);
	(void)A::Run(*run->configurator);
	run->done = true;
	return nullptr;
}

// Run() called itself once for each failing file: a directory of them ran the stack out.
// 5000 failures on a 256 KiB stack.
TEST_F(RunConfigs, ManyFailuresDoNotGrowTheStack)
{
	constexpr int kFiles = 5000;
	for (int i = 0; i < kFiles; ++i)
		(void)File("f" + std::to_string(i) + ".json");
	MojRefCountedPtr<RecordingConfigurator> c(new RecordingConfigurator(bus, dir));
	c->result = MojErrInvalidArg;

	StackRun run{c.get()};
	pthread_attr_t attr;
	ASSERT_EQ(0, pthread_attr_init(&attr));
	ASSERT_EQ(0, pthread_attr_setstacksize(&attr, 256 * 1024));
	pthread_t thread;
	ASSERT_EQ(0, pthread_create(&thread, &attr, RunOnSmallStack, &run));
	ASSERT_EQ(0, pthread_join(thread, nullptr));
	(void)pthread_attr_destroy(&attr);

	EXPECT_TRUE(run.done);
	EXPECT_EQ(static_cast<size_t>(kFiles), Configurator::ConfigureFailure().size());
}

} // namespace
