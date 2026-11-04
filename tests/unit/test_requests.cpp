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
// The scan, rescan and unconfigure calls: what an entry has to look like, and that a call is
// only kept for the final reply once all of it has been accepted. The bus answers a refused
// call itself; one kept as well was answered a second time at shutdown.

#include "fixture.h"

using namespace testutil;
using A = ConfiguratorTestAccess;
using Method = ConfiguratorTestAccess::Method;

namespace {

MojErr Parse(const char* json, std::vector<A::PackageRequest>& out, bool withTypes = false)
{
	return A::Parse(Json(json), withTypes, out);
}

TEST(PackageRequests, AnEntryNamesAnIdATypeAndALocation)
{
	std::vector<A::PackageRequest> r;
	ASSERT_EQ(MojErrNone, Parse("[{\"id\":\"com.example.app\",\"type\":\"app\",\"location\":\"third party\"},"
			"{\"id\":\"com.example.service\",\"type\":\"service\",\"location\":\"system\"}]", r));
	ASSERT_EQ(2u, r.size());
	EXPECT_STREQ("com.example.app", r[0].id.data());
	EXPECT_TRUE(A::IsApplication(r[0]));
	EXPECT_TRUE(A::IsThirdParty(r[0]));
	EXPECT_FALSE(A::IsApplication(r[1]));
	EXPECT_FALSE(A::IsThirdParty(r[1]));
	EXPECT_EQ(BusClient::ScanTypes(BusClient::ACTIVITIES | BusClient::FILECACHE | BusClient::DBKINDS | BusClient::DBPERMISSIONS), r[0].types);
}

// The id becomes a directory under usr/palm/; "../.." reached outside it.
TEST(PackageRequests, AnIdIsOnePathComponent)
{
	for (const char* id : {"", ".", "..", "../..", "../../etc", "a/b", "/abs", "com.example/../.."}) {
		std::vector<A::PackageRequest> r;
		const std::string json = std::string("[{\"id\":\"") + id + "\",\"type\":\"app\",\"location\":\"system\"}]";
		EXPECT_NE(MojErrNone, Parse(json.c_str(), r)) << id;
	}
	std::vector<A::PackageRequest> r;
	EXPECT_EQ(MojErrNone, Parse("[{\"id\":\"..hidden.but.fine\",\"type\":\"app\",\"location\":\"system\"}]", r));
}

TEST(PackageRequests, WhatIsMissingOrUnknownIsRefused)
{
	for (const char* json : {
			"{}",
			"[{\"type\":\"app\",\"location\":\"system\"}]",
			"[{\"id\":\"a\",\"location\":\"system\"}]",
			"[{\"id\":\"a\",\"type\":\"app\"}]",
			"[{\"id\":\"a\",\"type\":\"widget\",\"location\":\"system\"}]",
			"[{\"id\":\"a\",\"type\":\"app\",\"location\":\"moon\"}]"}) {
		std::vector<A::PackageRequest> r;
		EXPECT_NE(MojErrNone, Parse(json, r)) << json;
	}
}

TEST(PackageRequests, UnconfigureMayNameTheTypes)
{
	std::vector<A::PackageRequest> r;
	ASSERT_EQ(MojErrNone, Parse("[{\"id\":\"a\",\"type\":\"app\",\"location\":\"system\",\"types\":[\"dbkinds\",\"activities\"]}]", r, true));
	ASSERT_EQ(1u, r.size());
	EXPECT_EQ(BusClient::ScanTypes(BusClient::DBKINDS | BusClient::ACTIVITIES), r[0].types);

	r.clear();
	EXPECT_NE(MojErrNone, Parse("[{\"id\":\"a\",\"type\":\"app\",\"location\":\"system\",\"types\":[\"watches\"]}]", r, true));
}

class BusMethods : public ConfigDir {
protected:
	MojRefCountedPtr<FakeMessage> Message() { return MojRefCountedPtr<FakeMessage>(new FakeMessage(&A::Service(bus))); }
};

TEST_F(BusMethods, ARefusedCallIsNotKeptForASecondReply)
{
	for (Method m : {Method::Scan, Method::Rescan, Method::Unconfigure}) {
		MojRefCountedPtr<FakeMessage> msg = Message();
		// The second entry is bad; the first must not have been acted on either.
		const MojObject payload = Json("[{\"id\":\"com.example.app\",\"type\":\"app\",\"location\":\"system\"},"
				"{\"id\":\"../..\",\"type\":\"app\",\"location\":\"system\"}]");
		EXPECT_NE(MojErrNone, A::Call(bus, m, msg.get(), payload));
		EXPECT_EQ(nullptr, A::KeptMessage(bus));
		EXPECT_EQ(0u, A::Configurators(bus));
		EXPECT_EQ(0, msg->replies);
	}
}

TEST_F(BusMethods, AnAcceptedCallIsKeptAndScanned)
{
	MojRefCountedPtr<FakeMessage> msg = Message();
	EXPECT_EQ(MojErrNone, A::Call(bus, Method::Scan, msg.get(),
			Json("[{\"id\":\"com.example.app\",\"type\":\"app\",\"location\":\"system\"}]")));
	EXPECT_EQ(msg.get(), A::KeptMessage(bus));
	EXPECT_GT(A::Configurators(bus), 0u);
	EXPECT_EQ(0, msg->replies);
}

TEST_F(BusMethods, RunNeedsKnownTypes)
{
	MojRefCountedPtr<FakeMessage> msg = Message();
	EXPECT_NE(MojErrNone, A::Call(bus, Method::Run, msg.get(), Json("{\"types\":[\"watches\"]}")));
	EXPECT_NE(MojErrNone, A::Call(bus, Method::Run, msg.get(), Json("{\"types\":\"dbkinds\"}")));
	EXPECT_NE(MojErrNone, A::Call(bus, Method::Run, msg.get(), Json("{}")));
	EXPECT_EQ(nullptr, A::KeptMessage(bus));
}

} // namespace
