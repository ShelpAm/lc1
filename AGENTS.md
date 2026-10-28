# AGENTS.md

现有代码架构不代表最终设计。允许修改不合理的架构。

## Scope and workflow

- C++23, Vulkan 1.4 with vulkan-hpp / `vk::raii`, VMA-Hpp, GLFW and Slang.
  Linux first, Windows supported. No third-party rendering abstraction or volk.
- Read the relevant entry in [docs/README.md](docs/README.md) before editing a subsystem.
  Subsystem contracts belong in those documents; do not duplicate them here.
- Architecture may change to meet requirements. Update the affected contract with the code;
  do not treat current implementation as an immutable design or add speculative infrastructure.
- Keep changes scoped to the task. Preserve unrelated edits, including staged changes, and
  user comments and intentional code choices, including committed ones. Do not remove or
  rewrite them merely for cleanup or stylistic preference; suggest such changes instead.
- Investigate the full diagnostic and
  configuration; distinguish evidence from hypotheses. Fix causes without weakening contracts,
  removing `constexpr`, disabling checks or suppressing compiler jobs to hide failures.

## Build and verification

Use [BUILD.md](BUILD.md) for prerequisites, profiles, runtime environment and GPU test commands.

```sh
make
make run
ctest --test-dir build/linux-x86_64/Debug --output-on-failure --no-tests=error
```

- Keep build trees separate by platform, build type and toolchain; specify Conan build type.
- Check defaults before adding overrides. Local `JOBS=8` is a memory limit, not CI policy.
- Run checks appropriate to the change. Report compilation, CPU tests, GPU execution and
  platform coverage separately; local success does not establish remote CI success.
- Rendering checks require clean validation output. `LC1_NO_VALIDATION=1` is an explicit
  diagnostic/performance opt-out and cannot establish validation success.

## C++ conventions

- Use English in code, comments, diagnostics, UI, fixtures and scripts; Chinese in `docs/`.
- Follow the C++ Core Guidelines. Types/enumerators: `PascalCase`; functions, variables and
  constants: `snake_case`; private members: trailing `_`; macros: `ALL_CAPS`.
  No type/scope/storage prefixes; preserve Vulkan API spelling.
- Headers: `.hpp` with `#pragma once`; sources: `.cpp`. Run
  `clang-format --style=file -i <changed .hpp/.cpp files>` before finishing C++ changes.
- Prefer standard-library facilities, `const`/`constexpr` and views (`std::span`,
  `std::string_view`). Check preconditions, boundaries and return types.
- Use RAII rather than `init()`/`destroy()` or owning raw pointers. Non-owning members use
  pointers; document required non-null borrows and lifetimes. Reference parameters/returns are fine.
- No C-style casts. Explain the C API requirement for each `reinterpret_cast`.
- Use `fail()` / `lc1::Error`, without subclasses or parallel error-code/out-param schemes.
  Log `diagnostic()`, show `what()` in UI, and preserve `location()` when adding context.
  Let `vk::SystemError` carry Vulkan diagnostics without routine wrapping.
- Preserve both intentional Debug `throw;` handlers in `main`; Release returns `EXIT_FAILURE`.

## Required reading by change

| Change | Contract |
|---|---|
| Module boundaries, session update, application lifetime | [Code layout](docs/code-layout.md) |
| Gameplay goals, map, shared world and views | [Core systems](docs/content.md), [world map](docs/world-map.md), [continent](docs/continent.md) |
| Character intent, coordinates, import correction | [Character](docs/character.md) |
| Device input, actions, focus and cursor capture | [Input](docs/input.md) |
| CPU/GPU resource loading and borrowed lifetimes | [Resources](docs/resources.md) |
| Vulkan layers, dispatch, device and VMA setup | [Vulkan layers](docs/vulkan-layers.md) |
| Frame slots, swapchain, submission and synchronization | [Vulkan frame](docs/vulkan-minimal.md) |
| Shader stages, dynamic state, descriptor heaps | [Shader objects](docs/shader-objects.md), [heap interfaces](docs/heap-struct-generator.md), [heap tests](docs/descriptor-heap-test.md) |
| Materials, normal maps, MSAA and output encoding | [Lighting](docs/lighting.md) |
| BLAS/TLAS, shadow sampling and temporal history | [Ray-query shadows](docs/ray-query-shadows.md) |
| Dependencies, Conan and platform linking | [Build guide](BUILD.md) |
| CI, Slang, XKB, Wine or driver workarounds | [CI](docs/ci.md), [environment](docs/development-environment.md), [external defects](docs/external-defects.md) |

## Maintaining guidance

- Keep one primary document per contract; link instead of copying implementation summaries.
- Put dated investigations and superseded plans in [history](docs/history/README.md).
  Keep active external workarounds in [external defects](docs/external-defects.md).
- Distinguish implemented behavior from future plans. Do not remove a durable constraint
  merely because current code disagrees with it.
