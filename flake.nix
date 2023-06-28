{
  description = "Ledka";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nixpkgs-old.url = "github:NixOS/nixpkgs/21.11";

    esp-dev = {
      url = "github:mirrexagon/nixpkgs-esp-dev";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    esp-dev-old = {
      url = "github:mirrexagon/nixpkgs-esp-dev";
      inputs.nixpkgs.follows = "nixpkgs-old";
    };
  };

  outputs = inputs@{ flake-parts, nixpkgs-old, esp-dev, esp-dev-old, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-darwin" ];
      perSystem = { config, self', inputs', pkgs, system, ... }: {
        devShells.build = pkgs.mkShell {
          IDF_CCACHE_ENABLE = 1;
          hardeningDisable = [ "all" ];
          buildInputs = [
            # esp-dev shell
            esp-dev-old.packages.${system}.esp-idf
            esp-dev-old.packages.${system}.gcc-xtensa-esp32-elf-bin
            esp-dev-old.packages.${system}.gcc-riscv32-esp32c3-elf-bin
            esp-dev-old.packages.${system}.openocd-esp32-bin
            nixpkgs-old.legacyPackages.${system}.esptool-ck

            nixpkgs-old.legacyPackages.${system}.git
            nixpkgs-old.legacyPackages.${system}.wget
            nixpkgs-old.legacyPackages.${system}.gnumake

            nixpkgs-old.legacyPackages.${system}.flex
            nixpkgs-old.legacyPackages.${system}.bison
            nixpkgs-old.legacyPackages.${system}.gperf
            nixpkgs-old.legacyPackages.${system}.pkgconfig

            nixpkgs-old.legacyPackages.${system}.cmake
            nixpkgs-old.legacyPackages.${system}.ninja

            nixpkgs-old.legacyPackages.${system}.ncurses5

            # other
            nixpkgs-old.legacyPackages.${system}.ccache
          ];
        };
        devShells.default = pkgs.mkShell {
          hardeningDisable = [ "all" ];
          buildInputs = [
            pkgs.bear
            pkgs.clang-tools
            pkgs.cmake-format
            pkgs.nixfmt
            pkgs.picocom
          ];
        };
      };
    };
}
