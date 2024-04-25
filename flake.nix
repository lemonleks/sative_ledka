{
  description = "Ledka";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    esp-dev = {
      url = "github:mirrexagon/nixpkgs-esp-dev";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = inputs@{ flake-parts, esp-dev, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-darwin" ];
      perSystem = { config, self', inputs', pkgs, system, ... }:
        let
          fonts = pkgs.fetchzip {
            url =
              "https://jared.geek.nz/2014/01/custom-fonts-for-microcontrollers/files/fonts.zip";
            sha256 = "U3QX4oOECN05tvOcsp7dFWL5x21fzu2IdLN7D2EkUkg=";
          };
          python = pkgs.python3.withPackages (p: [ p.pillow ]);
        in {
          devShells.build = pkgs.mkShell {
            IDF_CCACHE_ENABLE = 1;
            hardeningDisable = [ "all" ];
            buildInputs = [
              esp-dev.packages.${system}.esp-idf-full
              pkgs.esptool-ck
              pkgs.ccache
            ];

            # Workaround since esp-idf brings own python
            PYTHON_FONTGEN = "${python}/bin/python";

            FONT_PATH = fonts;
          };
          devShells.default = pkgs.mkShell {
            hardeningDisable = [ "all" ];
            buildInputs = [
              # C
              pkgs.bear
              pkgs.clang-tools
              pkgs.cmake-format

              # Python
              python
              pkgs.black
              pkgs.pyright

              # Nix
              pkgs.nixfmt-classic

              # Tools
              pkgs.picocom
            ];
            FONT_PATH = fonts;
          };
        };
    };
}
