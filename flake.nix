# https://nix.dev/manual/nix/stable/command-ref/new-cli/nix3-flake#flake-format
# https://wiki.nixos.org/wiki/Flakes#Flake_schema
{
  inputs = {
    # https://nixos.org/manual/nixpkgs/unstable
    # https://search.nixos.org/packages?channel=unstable
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nix-appimage.url = "github:ralismark/nix-appimage/main";
    # https://github.com/ralismark/nix-appimage
    nix-appimage.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      nix-appimage,
    }:
    let
      systems = [
        "aarch64-linux"
        "x86_64-linux"
      ];

      genSystemAttrs = f: nixpkgs.lib.genAttrs systems f;
    in
    {
      # "nix build .#<attribute>"
      packages = genSystemAttrs (system: {
        default = self.packages.${system}.tera-launcher-for-linux;

        easylzma =
          with nixpkgs.legacyPackages.${system};
          stdenv.mkDerivation (finalAttrs: {
            pname = "easylzma";
            version = "0.0.9";

            src = fetchFromGitHub {
              owner = "PopusBenedictus";
              repo = "easylzma";
              rev = "master";
              hash = "sha256-FddX1Zpb3tlUAVSHfuQ+U2kevWNAWPxtWxbA8G/GHuM=";
            };

            strictDeps = true;

            nativeBuildInputs = [
              cmake
            ];

            installPhase = ''
              mkdir $out
              cp -r ${finalAttrs.pname}-${finalAttrs.version}/* $out
            '';

            meta = {
              description = "An easy to use, tiny, public domain, C wrapper library around Igor Pavlov's work that can be used to compress and extract lzma files";
              homepage = "https://github.com/PopusBenedictus/easylzma";
              license = lib.licenses.publicDomain;
              mainProgram = "elzma";
            };
          });

        # TODO: Fix build in Nix sandbox (move network access stuff to fixed output derivation/package)
        #
        # https://bmcgee.ie/posts/2023/02/nix-what-are-fixed-output-derivations-and-why-use-them
        tera-launcher-for-linux =
          with nixpkgs.legacyPackages.${system};
          stdenv.mkDerivation (finalAttrs: {
            pname = "tera-launcher-for-linux";
            version = "2025.04.11";

            src = lib.fileset.toSource {
              root = ./.;
              fileset = lib.fileset.unions [
                ./gui
                ./stub
                ./utils
                ./CMakeLists.txt
              ];
            };

            strictDeps = true;

            nativeBuildInputs = [
              cmake
              libarchive
              pkg-config
              python3
              wineWowPackages.stableFull
              winetricks
            ];

            buildInputs = [
              boost.dev
              curl.dev
              gtk4.dev
              jansson.dev
              libsecret.dev
              libtorrent-rasterbar.dev
              minixml
              openssl.dev
              protobufc.dev
              sqlite.dev
              # TODO: Fix build to source with find_package instead of ExternalProject_Add
              self.packages.${system}.easylzma
            ];

            meta = {
              description = "A community-created Linux launcher for TERA Online";
              homepage = "https://github.com/PopusBenedictus/tera-launcher-for-linux";
              license = lib.licenses.wtfpl;
              mainProgram = finalAttrs.pname;
            };
          });

        tera-launcher-for-linux-appimage =
          nix-appimage.bundlers.${system}.default
            self.packages.${system}.tera-launcher-for-linux;
      });

      # "nix develop"/"direnv allow"
      devShells = genSystemAttrs (system: {
        default = nixpkgs.legacyPackages.${system}.mkShell {
          packages = with nixpkgs.legacyPackages.${system}; [
            nix
            nix-update
            git
            git-lfs
            just
            treefmt
            nixfmt-rfc-style
            clang-tools
          ];

          # TODO: Remove when build fixed in Nix sandbox
          inputsFrom = with self.packages.${system}; [
            tera-launcher-for-linux
          ];
        };
      });
    };
}
