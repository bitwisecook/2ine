# Public domain. No rights reserved. Wholly AI-authored.
PYTHON ?= python3
MACOS_OUTPUT ?= $(CURDIR)/build-macos
MACOS_RUNTIME_BUILD ?= $(MACOS_OUTPUT)/runtime-build
MESA_PREFIX ?= $(CURDIR)/build-mesa-install
MESA_ICD ?= $(MESA_PREFIX)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json
VULKAN_LOADER ?= /opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
CMAKE_ARGS ?=
JOBS ?= 8
SIGN_IDENTITY ?=
NOTARY_PROFILE ?=

.PHONY: macos-app macos-app-dev macos-runtime macos-test macos-signing-check macos-engine

# Release target: never silently substitutes an ad-hoc signature or skips notarization.
macos-app: macos-signing-check macos-runtime
	$(PYTHON) tools/build_macos_app.py --output "$(MACOS_OUTPUT)" --runtime-build "$(MACOS_RUNTIME_BUILD)" --mesa-prefix "$(MESA_PREFIX)" --vulkan-loader "$(VULKAN_LOADER)" --sign-identity "$(SIGN_IDENTITY)" --notary-profile "$(NOTARY_PROFILE)" --notarize

macos-signing-check:
	@test -n "$(SIGN_IDENTITY)" || { echo 'Set SIGN_IDENTITY to a Developer ID Application identity.'; exit 2; }
	@test -n "$(NOTARY_PROFILE)" || { echo 'Set NOTARY_PROFILE to an existing notarytool Keychain profile.'; exit 2; }

macos-app-dev: macos-runtime
	$(PYTHON) tools/build_macos_app.py --output "$(MACOS_OUTPUT)" --runtime-build "$(MACOS_RUNTIME_BUILD)" --mesa-prefix "$(MESA_PREFIX)" --vulkan-loader "$(VULKAN_LOADER)"

macos-runtime:
	cmake -S . -B "$(MACOS_RUNTIME_BUILD)" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 -DTWOINE_PORTABLE_LOADER=ON -DTWOINE_SDL3_PROVIDER=system -DTWOINE_OPENGL=ON -DTWOINE_OPENGL_BACKEND=kosmickrisp -DTWOINE_MESA_ROOT="$(MESA_PREFIX)" -DTWOINE_KOSMICKRISP_ICD="$(MESA_ICD)" $(CMAKE_ARGS)
	cmake --build "$(MACOS_RUNTIME_BUILD)" --parallel $(JOBS)

macos-test: macos-engine
	swift test --package-path macos --scratch-path "$(MACOS_OUTPUT)/swift-tests"
	$(PYTHON) -m unittest discover -s tests/tools -p 'test_*.py'

macos-engine:
	cmake -S ball -B build-ball-engine -G Ninja
	cmake --build build-ball-engine

GNOME_RUNTIME_BUILD ?= $(CURDIR)/build/runtime
GNOME_OUTPUT ?= $(CURDIR)/build/gnome/packages
CARGO ?= cargo
.PHONY: gnome-app gnome-run gnome-packages gnome-test gnome-snap gnome-flatpak

gnome-app:
	$(CARGO) build --release --locked --manifest-path gnome/Cargo.toml

gnome-run:
	$(CARGO) run --manifest-path gnome/Cargo.toml -- --build "$(GNOME_RUNTIME_BUILD)"

gnome-packages: gnome-app
	$(PYTHON) tools/build_gnome_packages.py --build "$(GNOME_RUNTIME_BUILD)" --frontend build/gnome/release/2ine-ball --output "$(GNOME_OUTPUT)"

gnome-test:
	$(CARGO) fmt --manifest-path gnome/Cargo.toml --check
	$(CARGO) clippy --manifest-path gnome/Cargo.toml --all-targets -- -D warnings
	$(CARGO) test --manifest-path gnome/Cargo.toml

gnome-snap:
	snapcraft pack --use-lxd --output "$(CURDIR)/build/gnome/snap"

gnome-flatpak:
	flatpak-builder --user --install-deps-from=flathub --force-clean --state-dir build/flatpak/state --repo=build/flatpak/repo build/flatpak/app flatpak/io.github.bitwisecook.Twoine.Ball.yml
