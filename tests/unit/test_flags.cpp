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
// Flags<>: the set of scan types a call names.

#include "fixture.h"

namespace {

using Types = BusClient::ScanTypes;

static_assert(!Types());
static_assert(static_cast<bool>(Types(BusClient::DBKINDS)));
static_assert((BusClient::DBKINDS | BusClient::FILECACHE) == (Types(BusClient::FILECACHE) | BusClient::DBKINDS));
static_assert(!(Types(BusClient::DBKINDS) & BusClient::ACTIVITIES));

TEST(Flags, CombineAndTest)
{
	Types t;
	EXPECT_FALSE(t);
	t |= BusClient::DBPERMISSIONS;
	t |= BusClient::ACTIVITIES;
	EXPECT_TRUE(t & BusClient::DBPERMISSIONS);
	EXPECT_TRUE(t & BusClient::ACTIVITIES);
	EXPECT_FALSE(t & BusClient::DBKINDS);
	EXPECT_FALSE(t & BusClient::FILECACHE);

	t &= BusClient::ACTIVITIES;
	EXPECT_EQ(Types(BusClient::ACTIVITIES), t);
	t ^= BusClient::ACTIVITIES;
	EXPECT_TRUE(!t);
}

TEST(Flags, TheDefaultFileTypeIsNone)
{
	const BusClient::AdditionalFileTypes none = BusClient::None;
	EXPECT_FALSE(none & BusClient::DeprecatedDbKind);
	EXPECT_TRUE(BusClient::AdditionalFileTypes(BusClient::DeprecatedDbKind) & BusClient::DeprecatedDbKind);
}

} // namespace
