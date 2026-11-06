// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

// Do NOT include IrritatorCore.h or any other Unreal header here: the
// assertion macros of Unreal (`ensure`, `check`) collide with irritator (this
// TU is built with NoSharedPCHs and without unity build).

#include "IrritatorSession.h"

#include <irritator/archiver.hpp>
#include <irritator/core.hpp>
#include <irritator/file.hpp>
#include <irritator/format.hpp>
#include <irritator/global.hpp>
#include <irritator/modeling.hpp>
#include <irritator/simulation-player.hpp>
#include <irritator/thread.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

namespace irtue {

namespace {

constexpr auto run_slice = std::chrono::milliseconds(10);

struct component_dir {
    irt::name_str name;
    irt::path     path;
};

struct observed {
    irt::small_string<2 * irt::name_str::capacity()> name;
    irt::observer_id                                  id;
};

struct input {
    irt::name_str  name;
    irt::model_id  id;
};

struct output {
    irt::name_str name;
    irt::model_id id;
    bool          wanted  = false; ///< The game asks it, see events_mutex.
    bool          watched = false; ///< Thread of the simulation only.
};

bool is_directory(const irt::path& p) noexcept
{
    std::error_code ec;
    return std::filesystem::is_directory(p.to_std_path(), ec) and not ec;
}

bool add_registred(irt::file_access&      fs,
                   const std::string_view name,
                   const irt::path&       p) noexcept
{
    if (not is_directory(p) or not fs.registred_paths.can_alloc(1))
        return false;

    auto&      reg = fs.registred_paths.alloc();
    const auto id  = fs.registred_paths.get_id(reg);
    reg.name       = name;
    reg.path       = p.sv();
    fs.recorded_paths.emplace_back(id);
    return true;
}

} // namespace

struct session::impl {
    irt::vector<component_dir> dirs;

    // Rebuilt at each load: the player references the simulation of the
    // project and the project references the components of the modeling.
    std::unique_ptr<irt::modeling>          mod;
    std::unique_ptr<irt::project>           pj;
    std::unique_ptr<irt::command_journal>   journal;
    std::unique_ptr<irt::simulation_player> player;

    irt::vector<observed> observers;
    irt::vector<input>    inputs;
    irt::vector<output>   outputs;

    irt::table<irt::model_id, int> output_of;

    // Filled by the thread of the simulation between two steps, taken by the
    // game. The mutex guards the events and the wanted watches.
    std::mutex                 events_mutex;
    irt::vector<output_event>  events;
    std::size_t                events_head    = 0; ///< first event not taken
    std::size_t                event_capacity = 65536;
    std::atomic<std::uint64_t> dropped{ 0 };

    // Pushed by the thread of the game, applied by the thread that runs the
    // simulation between two steps.
    irt::command_queue commands;

    irt::log_history logs;

    std::atomic<session_state> state{ session_state::empty };
    std::atomic<double>        time{ 0.0 };
    std::atomic<bool>          stop{ false };

    std::string error;

    impl() noexcept
    {
        // One journal by thread, attached once: the registry never releases
        // them.
        if (not irt::current_journal)
            irt::journal_scope{};
    }

    void clear_error() noexcept { error.clear(); }

    bool fail(std::string_view msg) noexcept
    {
        try {
            error.assign(msg);
            append_logs();
        } catch (...) {
            error.clear();
        }
        return false;
    }

    bool fail(const irt::error_code& ec) noexcept
    {
        try {
            error.assign(irt::category_name(ec.cat()));
            error.append(" error ");
            error.append(std::to_string(ec.value()));
            append_logs();
        } catch (...) {
            error.clear();
        }
        return false;
    }

    bool fail_step(const irt::error_code& ec) noexcept
    {
        state = session_state::error;
        return fail(ec);
    }

    // The messages of a step only: the logs of the components that are not
    // used by the project (all the components of the directories are read)
    // would hide the error.
    void discard_logs() noexcept
    {
        logs.collect();
        logs.reset_history();
    }

    void append_logs()
    {
        logs.collect();

        irt::u64 version = 0, size = 0;
        logs.read_log(version, size, [&](std::span<const irt::log_record> l) {
            for (const auto& r : l) {
                if (r.level > irt::log_level::warning)
                    continue;
                error.append("\n");
                error.append(r.msg.sv());
            }
        });
        logs.reset_history();
    }

    void sync_state() noexcept
    {
        time = player->current_time();
        state =
          player->finished() ? session_state::finished : session_state::running;
    }

    irt::status init_modeling() noexcept
    {
        mod->files.write([&](irt::file_access& fs) noexcept {
            for (const auto& d : dirs)
                (void)add_registred(fs, d.name.sv(), d.path);

            if (auto p = irt::get_system_component_dir(); p)
                (void)add_registred(fs, "System directory", *p);
            if (auto p = irt::get_system_prefix_component_dir(); p)
                (void)add_registred(fs, "System prefix directory", *p);
            if (auto p = irt::get_default_user_component_dir(); p)
                (void)add_registred(fs, "User directory", *p);

            fs.browse_registreds();
        });

        return mod->fill_components();
    }

    irt::status read_project(const irt::path& p) noexcept
    {
        auto file =
          irt::file::open(p, irt::file_mode(irt::file_open_options::read));
        if (not file.has_value())
            return file.error();

        return mod->ids.read([&](const auto& ids, auto) -> irt::status {
            return mod->files.read([&](const auto& fs, auto) -> irt::status {
                return irt::load_project(*pj, fs, ids, *file, p.sv());
            });
        });
    }

    bool list_observers() noexcept
    {
        observers.clear();

        for (const auto& vo : pj->variable_observers) {
            auto index = 0;
            for (const auto id : vo.subs) {
                auto* o = observers.emplace_back();
                if (not o)
                    return false;

                o->id = vo.subs.get<irt::observer_id>(id);

                const auto sub = vo.subs.get<irt::name_str>(id).sv();
                if (sub.empty())
                    irt::format(o->name, "{}/{}", vo.name.sv(), index);
                else
                    irt::format(o->name, "{}/{}", vo.name.sv(), sub);

                ++index;
            }
        }

        return true;
    }

    // The constants that are parameters of the project: the inputs of the
    // game.
    bool list_inputs() noexcept
    {
        inputs.clear();

        const auto& names = pj->parameters.get<irt::name_str>();
        const auto& mdls  = pj->parameters.get<irt::model_id>();

        for (const auto id : pj->parameters) {
            const auto  idx = irt::get_index(id);
            const auto* mdl = pj->sim.models.try_to_get(mdls[idx]);

            if (not mdl or mdl->type != irt::dynamics_type::constant)
                continue;

            auto* in = inputs.emplace_back();
            if (not in)
                return false;

            in->name = names[idx];
            in->id   = mdls[idx];
        }

        return true;
    }

    // The observables of the project: the outputs of the game.
    bool list_outputs() noexcept
    {
        outputs.clear();
        output_of.data.clear();

        const auto& names = pj->observables.get<irt::name_str>();
        const auto& mdls  = pj->observables.get<irt::model_id>();

        for (const auto id : pj->observables) {
            const auto idx = irt::get_index(id);
            if (not pj->sim.models.try_to_get(mdls[idx]))
                continue;

            auto* out = outputs.emplace_back();
            if (not out)
                return false;

            out->name = names[idx];
            out->id   = mdls[idx];

            if (not output_of.data.emplace_back(
                  out->id, static_cast<int>(outputs.size()) - 1))
                return false;
        }

        output_of.sort();
        return true;
    }

    // After a new build of the simulation the identifiers may change: the
    // watches of the game are kept by name.
    bool relist() noexcept
    {
        irt::vector<irt::name_str> wanted;
        std::scoped_lock           lock(events_mutex);

        for (const auto& out : outputs)
            if (out.wanted and not wanted.emplace_back(out.name))
                return false;

        pj->sim.unwatch_all();

        if (not list_observers() or not list_inputs() or not list_outputs())
            return false;

        for (auto& out : outputs)
            out.wanted = std::ranges::any_of(
              wanted, [&](const auto& n) { return n.sv() == out.name.sv(); });

        return true;
    }

    void discard_events() noexcept
    {
        std::scoped_lock lock(events_mutex);
        events.clear();
        events_head = 0;
        pj->sim.clear_watched_events();
    }

    // Before a step, in the thread of the simulation.
    bool sync_watches() noexcept
    {
        std::scoped_lock lock(events_mutex);

        for (auto& out : outputs) {
            if (out.wanted == out.watched)
                continue;

            if (out.wanted) {
                if (auto ret = pj->sim.watch(out.id); ret.has_error()) {
                    out.wanted = false;
                    return fail(ret.error());
                }
            } else {
                pj->sim.unwatch(out.id);
            }

            out.watched = out.wanted;
        }

        return true;
    }

    // After a step, in the thread of the simulation.
    void publish_events() noexcept
    {
        const auto ev = pj->sim.watched_events();
        if (ev.empty())
            return;

        std::scoped_lock lock(events_mutex);

        for (const auto& e : ev) {
            const auto* index = output_of.get(e.mdl);
            if (not index or not outputs[*index].wanted)
                continue;

            if (events.size() - events_head >= event_capacity) {
                ++events_head;
                ++dropped;
            }

            // Without memory the event is dropped: the game goes on.
            if (not events.emplace_back(output_event{ .time   = e.t,
                                                      .output = *index,
                                                      .port   = e.port,
                                                      .value  = e.value }))
                ++dropped;
        }

        pj->sim.clear_watched_events();
        compact_events();
    }

    // The taken and dropped events are removed once they are the half of the
    // buffer: the cost of the copy is shared by the events.
    void compact_events() noexcept
    {
        if (events_head == 0 or events_head * 2 < events.size())
            return;

        const auto first = events.begin() + events_head;
        std::copy(first, events.end(), events.begin());
        (void)events.resize(events.size() - events_head);
        events_head = 0;
    }

    void discard_commands() noexcept
    {
        commands.consume([](const irt::command&) noexcept {});
    }

    // A command that fails is reported but does not stop the simulation.
    bool apply_commands() noexcept
    {
        if (commands.empty())
            return true;

        if (auto ret = player->edit([&]() noexcept {
                return journal->apply(commands, player->step());
            });
            ret.has_error()) {
            discard_commands();
            return fail(ret.error());
        }

        return true;
    }

    // The game never goes back in time: without keyframes the memory does not
    // grow with the length of the simulation.
    irt::status start() noexcept
    {
        player->reset();
        irt_check(player->keep_history(false));
        irt_check(player->start());

        time  = player->current_time();
        state = session_state::loaded;

        return irt::success();
    }
};

session::session() noexcept
{
    try {
        m_impl = std::make_unique<impl>();
    } catch (...) {
    }
}

session::~session() noexcept = default;

bool session::add_component_dir(std::string_view name_utf8,
                                std::string_view path_utf8) noexcept
{
    if (not m_impl)
        return false;

    m_impl->clear_error();

    if (path_utf8.size() >= irt::path::capacity() or
        name_utf8.size() >= irt::name_str::capacity())
        return m_impl->fail("component directory name or path too long");

    auto* d = m_impl->dirs.emplace_back();
    if (not d)
        return m_impl->fail("out of memory");

    d->name = name_utf8;
    d->path = path_utf8;
    return true;
}

bool session::load(std::string_view path_utf8) noexcept
{
    if (not m_impl)
        return false;

    auto& s = *m_impl;
    s.clear_error();
    s.discard_logs();
    s.stop  = false;
    s.state = session_state::empty;
    s.observers.clear();
    s.inputs.clear();
    s.discard_commands();
    {
        std::scoped_lock lock(s.events_mutex);
        s.outputs.clear();
        s.output_of.data.clear();
        s.events.clear();
        s.events_head = 0;
    }
    s.dropped = 0;
    s.player.reset();
    s.journal.reset();
    s.pj.reset();
    s.mod.reset();

    if (path_utf8.size() >= irt::path::capacity())
        return s.fail("path too long");

    const irt::path p(path_utf8);

    {
        std::error_code ec;
        if (not std::filesystem::is_regular_file(p.to_std_path(), ec))
            return s.fail(ec ? "cannot access the project file"
                             : "project file not found or not a regular file");
    }

    try {
        s.mod     = std::make_unique<irt::modeling>();
        s.pj      = std::make_unique<irt::project>();
        s.journal = std::make_unique<irt::command_journal>(*s.pj);
    } catch (...) {
        return s.fail("out of memory");
    }

    if (auto ret = s.init_modeling(); ret.has_error())
        return s.fail(ret.error());

    s.discard_logs();

    if (auto ret = s.read_project(p); ret.has_error())
        return s.fail(ret.error());

    if (auto ret = s.pj->simulation_init(*s.mod); ret.has_error())
        return s.fail(ret.error());

    try {
        s.player = std::make_unique<irt::simulation_player>(s.pj->sim);
    } catch (...) {
        return s.fail("out of memory");
    }

    s.player->set_edition_replay(s.journal.get());

    bool listed = false;
    {
        std::scoped_lock lock(s.events_mutex);
        listed = s.list_observers() and s.list_inputs() and s.list_outputs();
    }

    if (not listed)
        return s.fail("out of memory");

    if (auto ret = s.start(); ret.has_error())
        return s.fail_step(ret.error());

    return true;
}

bool session::restart() noexcept
{
    if (not m_impl)
        return false;

    m_impl->clear_error();
    m_impl->discard_logs();
    m_impl->discard_commands();
    m_impl->stop = false;

    if (not m_impl->player)
        return m_impl->fail("no project loaded");

    m_impl->journal->clear();

    if (auto ret = m_impl->start(); ret.has_error())
        return m_impl->fail_step(ret.error());

    // After the start: the events of the previous run are not for the game.
    m_impl->discard_events();
    return true;
}

bool session::replay() noexcept
{
    if (not m_impl)
        return false;

    auto& s = *m_impl;
    s.clear_error();
    s.discard_logs();
    s.discard_commands();
    s.stop = false;

    if (not s.player)
        return s.fail("no project loaded");

    // A new build: the editions of the game are not in the simulation any
    // more, the journal does them again.
    if (auto ret = s.pj->simulation_init(*s.mod); ret.has_error()) {
        s.state = session_state::error;
        return s.fail(ret.error());
    }

    if (not s.relist())
        return s.fail("out of memory");

    if (auto ret = s.start(); ret.has_error())
        return s.fail_step(ret.error());

    s.discard_events();
    return true;
}

bool session::save_journal(std::string_view path_utf8) noexcept
{
    if (not m_impl)
        return false;

    auto& s = *m_impl;
    s.clear_error();

    if (not s.player)
        return s.fail("no project loaded");

    if (path_utf8.size() >= irt::path::capacity())
        return s.fail("path too long");

    auto file = irt::file::open(
      irt::path(path_utf8), irt::file_mode(irt::file_open_options::write));
    if (not file.has_value())
        return s.fail(file.error());

    if (auto ret = irt::save_journal(*s.pj, *s.journal, *file,
                                     irt::json_pretty_print::indent_2);
        ret.has_error())
        return s.fail(ret.error());

    return true;
}

bool session::load_journal(std::string_view path_utf8) noexcept
{
    if (not m_impl)
        return false;

    auto& s = *m_impl;
    s.clear_error();

    if (not s.player)
        return s.fail("no project loaded");

    if (path_utf8.size() >= irt::path::capacity())
        return s.fail("path too long");

    auto file = irt::file::open(
      irt::path(path_utf8), irt::file_mode(irt::file_open_options::read));
    if (not file.has_value())
        return s.fail(file.error());

    if (auto ret = irt::load_journal(*s.pj, *s.journal, *file);
        ret.has_error()) {
        s.journal->clear();
        return s.fail(ret.error());
    }

    return replay();
}

bool session::advance_until(double                   target,
                            std::chrono::nanoseconds budget) noexcept
{
    if (not m_impl)
        return false;

    auto& s = *m_impl;
    s.clear_error();

    if (not s.player)
        return s.fail("no project loaded");

    if (s.state == session_state::error)
        return s.fail("the simulation failed: restart it");

    if (s.state == session_state::finished)
        return true;

    s.discard_logs();
    const bool commands_ok = s.apply_commands();
    const bool watches_ok  = s.sync_watches();

    const auto ret = s.player->advance_until(target, budget);
    s.publish_events();

    if (ret.has_error())
        return s.fail_step(ret.error());

    s.sync_state();
    return commands_ok and watches_ok;
}

bool session::run_to_end() noexcept
{
    if (not m_impl)
        return false;

    auto& s = *m_impl;
    s.clear_error();
    s.discard_logs();

    if (not s.player)
        return s.fail("no project loaded");

    if (s.player->is_live())
        return s.fail("the project has no end date");

    if (s.state == session_state::error)
        return s.fail("the simulation failed: restart it");

    bool commands_ok = true;

    while (not s.player->finished() and not s.stop) {
        if (not s.apply_commands())
            commands_ok = false;

        if (not s.sync_watches())
            commands_ok = false;

        const auto ret = s.player->advance(run_slice);
        s.publish_events();

        if (ret.has_error())
            return s.fail_step(ret.error());

        s.sync_state();
    }

    // Cleared at the end: a request sent before the start of the run stops
    // it too.
    s.stop = false;
    s.sync_state();
    return commands_ok;
}

int session::input_count() const noexcept
{
    return m_impl ? static_cast<int>(m_impl->inputs.size()) : 0;
}

std::string_view session::input_name(int index) const noexcept
{
    if (not m_impl or index < 0 or index >= input_count())
        return {};

    return m_impl->inputs[index].name.sv();
}

int session::find_input(std::string_view name) const noexcept
{
    for (int i = 0, e = input_count(); i < e; ++i)
        if (m_impl->inputs[i].name.sv() == name)
            return i;

    return -1;
}

bool session::send_value(int index, double value) noexcept
{
    if (not m_impl or index < 0 or index >= input_count())
        return false;

    const auto st = m_impl->state.load();
    if (st != session_state::loaded and st != session_state::running)
        return false;

    return m_impl->commands.push(irt::command{
      .type = irt::command_type::send_message,
      .data{ .send_message{ .mdl_id = m_impl->inputs[index].id,
                            .value  = value } } });
}

void session::request_stop() noexcept
{
    if (m_impl)
        m_impl->stop = true;
}

session_state session::state() const noexcept
{
    return m_impl ? m_impl->state.load() : session_state::empty;
}

double session::current_time() const noexcept
{
    return m_impl ? m_impl->time.load() : 0.0;
}

double session::begin_time() const noexcept
{
    return m_impl and m_impl->player ? m_impl->player->begin_time() : 0.0;
}

double session::end_time() const noexcept
{
    return m_impl and m_impl->player ? m_impl->player->end_time() : 0.0;
}

int session::observer_count() const noexcept
{
    return m_impl ? static_cast<int>(m_impl->observers.size()) : 0;
}

std::string_view session::observer_name(int index) const noexcept
{
    if (not m_impl or index < 0 or index >= observer_count())
        return {};

    return m_impl->observers[index].name.sv();
}

int session::find_observer(std::string_view name) const noexcept
{
    for (int i = 0, e = observer_count(); i < e; ++i)
        if (m_impl->observers[i].name.sv() == name)
            return i;

    return -1;
}

double session::observer_value(int index) const noexcept
{
    if (not m_impl or not m_impl->pj or index < 0 or index >= observer_count())
        return 0.0;

    const auto  id  = m_impl->observers[index].id;
    const auto* obs = m_impl->pj->sim.observers.try_to_get(id);

    return obs ? obs->last_value() : 0.0;
}

int session::output_count() const noexcept
{
    return m_impl ? static_cast<int>(m_impl->outputs.size()) : 0;
}

std::string_view session::output_name(int index) const noexcept
{
    if (not m_impl or index < 0 or index >= output_count())
        return {};

    return m_impl->outputs[index].name.sv();
}

int session::find_output(std::string_view name) const noexcept
{
    for (int i = 0, e = output_count(); i < e; ++i)
        if (m_impl->outputs[i].name.sv() == name)
            return i;

    return -1;
}

bool session::watch_output(int index, bool enable) noexcept
{
    if (not m_impl)
        return false;

    std::scoped_lock lock(m_impl->events_mutex);

    if (index < 0 or index >= static_cast<int>(m_impl->outputs.size()))
        return false;

    m_impl->outputs[index].wanted = enable;
    return true;
}

std::size_t session::take_events(std::span<output_event> out) noexcept
{
    if (not m_impl or out.empty())
        return 0;

    auto&            s = *m_impl;
    std::scoped_lock lock(s.events_mutex);

    const auto n = std::min(out.size(), s.events.size() - s.events_head);
    std::copy_n(s.events.begin() + s.events_head, n, out.begin());
    s.events_head += n;

    if (s.events_head == s.events.size()) {
        s.events.clear();
        s.events_head = 0;
    }

    return n;
}

void session::set_event_capacity(std::size_t capacity) noexcept
{
    if (not m_impl)
        return;

    std::scoped_lock lock(m_impl->events_mutex);
    m_impl->event_capacity = std::max<std::size_t>(capacity, 1);
}

std::uint64_t session::dropped_events() const noexcept
{
    return m_impl ? m_impl->dropped.load() : 0;
}

std::string_view session::last_error() const noexcept
{
    return m_impl ? std::string_view(m_impl->error)
                  : std::string_view("session not initialized");
}

} // namespace irtue
