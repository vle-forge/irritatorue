// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

// Only the platform header of Unreal: it defines the export macro without the
// assertion macros (`ensure`, `check`) that collide with irritator.
#include "HAL/Platform.h"

#include <chrono>
#include <memory>
#include <string_view>

namespace irtue {

enum class session_state {
    empty,    ///< No project, or the last load failed.
    loaded,   ///< The project is ready at its begin date.
    running,  ///< At least one step is done, the end is not reached.
    finished, ///< The end date is reached.
    error     ///< A step failed: load or restart the project.
};

/**
 * A project of irritator (a @c .pirt file and its components) and the player
 * of its simulation.
 *
 * The session is not thread safe: a thread calls @c run_to_end() while the
 * others only call @c request_stop(), @c state(), @c current_time() and @c
 * observer_value(). The other functions must wait for the end of the run.
 */
class IRRITATORCORE_API session
{
public:
    session() noexcept;
    ~session() noexcept;

    session(const session&)            = delete;
    session& operator=(const session&) = delete;

    /**
     * Adds a directory of components searched before the directories of the
     * installation of irritator. A component is found by the name of the
     * directory written in the project, so the directory @c path must contain
     * the sub directories of the components (@c path/<dir>/<file>.irt).
     *
     * @param name The name of the directory in the projects, used before the
     * search by sub directory.
     */
    bool add_component_dir(std::string_view name_utf8,
                           std::string_view path_utf8) noexcept;

    /// Reads the project and initializes its simulation at its begin date.
    bool load(std::string_view path_utf8) noexcept;

    /// Starts the simulation of the loaded project again from its begin date.
    bool restart() noexcept;

    /**
     * Runs the events up to the date @c target (included) and moves the clock
     * to @c target. The call stops when @c budget is spent: the clock is then
     * before @c target and the next call goes on.
     */
    bool advance_until(double                   target,
                       std::chrono::nanoseconds budget) noexcept;

    /**
     * Runs the simulation to its end date, or until @c request_stop(). The
     * project must have an end date.
     */
    bool run_to_end() noexcept;

    /// Asks @c run_to_end() to return as soon as possible. Thread safe.
    void request_stop() noexcept;

    session_state state() const noexcept;

    double current_time() const noexcept;
    double begin_time() const noexcept;
    double end_time() const noexcept;

    /// The models observed by the project, named @c "<observer>/<model>".
    int              observer_count() const noexcept;
    std::string_view observer_name(int index) const noexcept;
    int              find_observer(std::string_view name) const noexcept;

    /// The value of the last observation of the model. Thread safe.
    double observer_value(int index) const noexcept;

    /// The message of the last error, with the messages of irritator.
    std::string_view last_error() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

} // namespace irtue
