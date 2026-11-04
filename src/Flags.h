// Copyright (c) 2009-2019 LG Electronics, Inc.
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

#ifndef FLAGS_H
#define FLAGS_H

#include <type_traits>

// A set of values of one enum, combined with | and tested with &.
template <class Enum>
class Flags {
public:
	using enum_type = Enum;
	using value_type = std::make_unsigned_t<std::underlying_type_t<Enum>>;

	constexpr Flags() = default;
	constexpr Flags(Enum flag) : m_flags(static_cast<value_type>(flag)) {}

	constexpr explicit operator bool() const { return m_flags != 0; }
	constexpr bool operator!() const { return m_flags == 0; }

	constexpr Flags operator&(Flags mask) const { return Flags(m_flags & mask.m_flags); }
	constexpr Flags operator|(Flags other) const { return Flags(m_flags | other.m_flags); }
	constexpr Flags operator^(Flags other) const { return Flags(m_flags ^ other.m_flags); }

	constexpr Flags& operator&=(Flags mask) { m_flags &= mask.m_flags; return *this; }
	constexpr Flags& operator|=(Flags other) { m_flags |= other.m_flags; return *this; }
	constexpr Flags& operator^=(Flags other) { m_flags ^= other.m_flags; return *this; }

	constexpr bool operator==(const Flags&) const = default;

private:
	constexpr explicit Flags(value_type flags) : m_flags(flags) {}

	value_type m_flags = 0;
};

#define DECLARE_FLAGS(Name, Enum) using Name = Flags<Enum>

// Lets two values of the enum, or a value and a set, be combined with |.
#define DECLARE_OPERATORS_FOR_FLAGS(Name) \
constexpr Name operator|(Name::enum_type e1, Name::enum_type e2) { return Name(e1) | e2; } \
constexpr Name operator|(Name::enum_type e1, Name f2) { return f2 | e1; }

#endif // FLAGS_H
