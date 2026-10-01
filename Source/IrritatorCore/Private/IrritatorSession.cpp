// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

// Do NOT include IrritatorCore.h or any Unreal header here: this TU relies on
// the UE `ensure` macro being undefined (NoSharedPCHs, no unity build).

#include "IrritatorSession.h"

#include <irritator/core.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace irtue {

struct session::impl {
    irt::modeling mod;
    irt::project pj;

    std::string error;
    bool loaded = false;

    void set_error(std::string_view msg) noexcept
    {
        try {
            error.assign(msg);
        } catch (...) {
            error.clear();
        }
    }
};

session::session() noexcept
{
    try {
        m_impl = std::make_unique<impl>();
    } catch (...) {
        // m_impl stays null: every method below handles that case.
    }
}

session::~session() noexcept = default;

bool session::load(std::string_view path_utf8) noexcept
{
    if (!m_impl)
        return false;

    m_impl->loaded = false;

    if (path_utf8.size() >= irt::path::capacity()) {
        m_impl->set_error("path too long");
        return false;
    }

    irt::path p(path_utf8);

    try {
        const auto std_path = p.to_std_path();

        std::error_code ec;
        if (not std::filesystem::is_regular_file(std_path, ec)) {
            m_impl->set_error(ec ? "cannot access file"
                                 : "file not found or not a regular file");
            return false;
        }

        // TODO: call Irritator's real model loader here and report its
        // failure through m_impl->set_error(...).

        m_impl->loaded = true;
        return true;
    } catch (...) {
        m_impl->set_error("out of memory");
        return false;
    }
}

bool session::is_loaded() const noexcept
{
    return m_impl and m_impl->loaded;
}

std::string_view session::last_error() const noexcept
{
    return m_impl ? std::string_view(m_impl->error)
                  : std::string_view("session not initialized");
}

} // namespace irtue