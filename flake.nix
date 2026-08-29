# SPDX-FileCopyrightText: 2026 Leonardo Nobre <info@nobrehd.pt>
# SPDX-License-Identifier: LGPL-2.0-or-later

{
    description = "Theme-Aware SVG Thumbnailer for KDE Plasma 6";

    inputs = {
        nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    };

    outputs = { self, nixpkgs }:
        let
            systems = [ "x86_64-linux" "aarch64-linux" ];
            forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
        in
        {
            packages = forAllSystems (pkgs: rec {
                default = kio-tasvg;

                kio-tasvg = pkgs.stdenv.mkDerivation {
                    pname = "kio-tasvg";
                    version = "1.0.0";

                    src = ./.;

                    nativeBuildInputs = [
                        pkgs.cmake
                        pkgs.kdePackages.extra-cmake-modules
                        pkgs.kdePackages.wrapQtAppsHook
                    ];

                    buildInputs = [
                        pkgs.qt6.qtbase
                        pkgs.qt6.qtsvg
                        pkgs.kdePackages.kio
                        pkgs.kdePackages.karchive
                        pkgs.kdePackages.kconfig
                    ];

                    meta = with pkgs.lib; {
                        description = "Theme-Aware SVG Thumbnailer for KDE Plasma 6";
                        homepage = "https://github.com/NobreHD/kio-tasvg";
                        license = licenses.lgpl2Plus;
                        platforms = platforms.linux;
                        mainProgram = "tasvg-daemon";
                    };
                };
            });

            devShells = forAllSystems (pkgs: {
                default = pkgs.mkShell {
                    inputsFrom = [ self.packages.${pkgs.system}.default ];
                };
            });

            overlays.default = final: prev: {
                kio-tasvg = self.packages.${prev.stdenv.hostPlatform.system}.default;
            };

            nixosModules.default = { config, lib, pkgs, ... }:
                let
                    cfg = config.programs.kio-tasvg;
                    defaultPkg = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
                in
                {
                    options.programs.kio-tasvg = {
                        enable = lib.mkEnableOption "Theme-Aware SVG Thumbnailer for KDE Plasma 6";

                        package = lib.mkOption {
                            type = lib.types.package;
                            default = defaultPkg;
                            description = "The kio-tasvg package to install.";
                        };

                        autoSync = lib.mkOption {
                            type = lib.types.bool;
                            default = true;
                            description = "Enable background daemon for automatic Day/Night thumbnail sync.";
                        };
                    };

                    config = lib.mkIf cfg.enable {
                        environment.systemPackages = [ cfg.package ];

                        environment.sessionVariables.QT_PLUGIN_PATH = [
                            "${cfg.package}/${pkgs.qt6.qtbase.qtPluginPrefix}"
                        ];

                        systemd.user.services.tasvg-daemon = lib.mkIf cfg.autoSync {
                            description = "Theme-Aware SVG Thumbnail Cache Sync Daemon";
                            wantedBy = [ "graphical-session.target" ];
                            partOf = [ "graphical-session.target" ];
                            after = [ "graphical-session.target" ];
                            serviceConfig = {
                                ExecStart = "${cfg.package}/bin/tasvg-daemon";
                                Restart = "on-failure";
                            };
                        };
                    };
                };
        };
}