# IrritatorUE

An Unreal Engine 5.7 plug-in to build games and simulations with
[irritator](https://github.com/vle-forge/irritator), a DEVS (Discrete Event
System Specification) modeling and simulation kernel.

A project of irritator (a `.pirt` file and its components, built with the
irritator user interface) is loaded and simulated inside a world: the game
sends values to the simulation, reads its observations and receives its
events.

## Modules

- `IrritatorCore`: `irtue::session`, a project of irritator and the player of
  its simulation behind a C++ interface without Unreal and irritator types.
  The headers of irritator never meet the assertion macros of Unreal
  (`ensure`, `check`).
- `IrritatorRuntime`: the world subsystem `UIrritatorSubsystem` and the
  component `UIrritatorObserverComponent`, for Blueprint and C++.
- `IrritatorEditor`: the editor module.
- `Source/ThirdParty/irritator`: the sources of irritator, a git submodule,
  compiled into `IrritatorCore`.

## Usage

The subsystem simulates one project by world:

```cpp
UIrritatorSubsystem* Sub = GetWorld()->GetSubsystem<UIrritatorSubsystem>();
Sub->LoadProject(TEXT("my-game/model.pirt")); // in Content/Irritator
Sub->Play();
```

- **Time**: `Play()` simulates `DeltaTime * TimeScale` units of time at each
  frame, within `FrameBudgetMs`. `RunToEnd()` runs the simulation to its end
  date in a background thread, then broadcasts `OnSimulationFinished`.
- **Inputs**: the constants marked *configurable* in irritator.
  `SendValue(Name, Value)` makes the constant send a new value at the current
  date of the simulation (`GetInputNames()`).
- **Outputs**: the models marked *observable* in irritator. An
  `UIrritatorObserverComponent` with `OutputName` receives `OnEvent(Time, Port,
  Value)`: a message of the model on its output port (a threshold crossed, a
  generator that emits) or, for a counter, its new value with `Port = -1`.
  Only the outputs with a component are watched (`GetOutputNames()`).
- **Values**: the observers of the project give the value of a continuous
  model, an integrator for example: `GetObserverValue("observer/model")`
  (`GetObserverNames()`).

The projects and their components are searched in `Content/Irritator` of the
Unreal project, in the directories added by `AddComponentDirectory()`, then in
the directories of an installation of irritator. `Content/Irritator` is
staged as is, outside the pak files, in a packaged game.

## Build

```sh
git clone --recursive git@github.com:vle-forge/irritatorue.git
cd irritatorue
python Source/IrritatorCore/link_vendor.py   # links Private/vendor/irritator-lib
```

Run `link_vendor.py` again after an update of the submodule. Then put the
plug-in in the `Plugins` directory of a project (a link is enough), or build
it alone:

```sh
UE=~/apps/UnrealEngine-5.7
$UE/Engine/Build/BatchFiles/RunUAT.sh BuildPlugin \
  -Plugin=$PWD/IrritatorUE.uplugin -Package=/tmp/IrritatorUE -TargetPlatforms=Linux
```

The automation tests of the plug-in, from a project that uses it:

```sh
$UE/Engine/Binaries/Linux/UnrealEditor-Cmd MyProject.uproject \
  -ExecCmds="Automation RunTests Irritator; Quit" -unattended -nullrhi
```

## Demo

[irritatorue-demo](https://github.com/vle-forge/irritatorue-demo) is a
playable demo: a tank filled by a simulation, with an input, an observed value
and two outputs.

## License

Boost Software License 1.0, see `LICENSE`.
