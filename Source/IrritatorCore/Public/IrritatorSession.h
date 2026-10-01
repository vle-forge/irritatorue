// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <memory>
#include <string_view>

namespace irtue {

class session
{
public:
    IRRITATORCORE_API session() noexcept;
    IRRITATORCORE_API ~session() noexcept;

    session(const session&) noexcept = delete;
    session& operator=(const session&) noexcept = delete;

    IRRITATORCORE_API bool load(std::string_view path_utf8) noexcept;
    IRRITATORCORE_API bool is_loaded() const noexcept;
    IRRITATORCORE_API std::string_view last_error() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

} // namespace irtue