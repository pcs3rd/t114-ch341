{
  description = "Heltec T114 firmware that emulates a CH341 USB-SPI LoRa bridge (MeshStick-style) for meshtasticd";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Pinned to the same revisions build.sh / tools/get_deps.py use
    tinyusb = { url = "github:hathach/tinyusb/93aa62bce0f2708c9d0794b1947f50f9f1a0ac0d"; flake = false; };
    nrfx = { url = "github:NordicSemiconductor/nrfx/11f57e578c7feea13f21c79ea0efab2630ac68c7"; flake = false; };
    cmsis5 = { url = "github:ARM-software/CMSIS_5/2b7495b8535bdcb306dac29b9ded4cfb679d7e5c"; flake = false; };
    uf2 = { url = "github:microsoft/uf2/c594542b2faa01cc33a2b97c9fbebc38549df80a"; flake = false; };
    # meshtasticd's host-side CH341 library, used only by the test harness
    libch341 = { url = "github:meshtastic/libch341-spi-userspace/027cde5d9c0cecc91fffbd2d1ed4e41e32eeda79"; flake = false; };
  };

  outputs = { self, nixpkgs, tinyusb, nrfx, cmsis5, uf2, libch341 }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAll (pkgs: rec {
        firmware = pkgs.stdenvNoCC.mkDerivation {
          pname = "t114-ch341";
          version = self.shortRev or self.dirtyShortRev or "dev";
          src = self;
          nativeBuildInputs = [ pkgs.gcc-arm-embedded pkgs.gnumake pkgs.python3 ];
          dontConfigure = true;
          dontFixup = true;

          buildPhase = ''
            runHook preBuild
            cp -r ${tinyusb} tusb && chmod -R u+w tusb
            mkdir -p tusb/hw/mcu/nordic tusb/lib
            rm -rf tusb/hw/mcu/nordic/nrfx tusb/lib/CMSIS_5 tusb/tools/uf2
            cp -r ${nrfx} tusb/hw/mcu/nordic/nrfx
            cp -r ${cmsis5} tusb/lib/CMSIS_5
            cp -r ${uf2} tusb/tools/uf2
            chmod -R u+w tusb
            cp -r board/heltec_t114 tusb/hw/bsp/nrf/boards/heltec_t114
            cp -r app tusb/examples/device/t114_ch341
            make -C tusb/examples/device/t114_ch341 BOARD=heltec_t114 -j$NIX_BUILD_CORES all uf2
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm644 -t $out tusb/examples/device/t114_ch341/_build/heltec_t114/t114_ch341.{uf2,hex,elf}
            install -Dm644 -t $out lora-usb-heltec-t114.yaml
            runHook postInstall
          '';
        };
        default = firmware;
      });

      # `nix flake check` runs the host-side protocol test against the real libpinedio
      checks = forAll (pkgs: {
        protocol = pkgs.stdenv.mkDerivation {
          name = "t114-ch341-protocol-test";
          src = self;
          buildPhase = ''
            cd test
            make LIBCH341=${libch341} run
          '';
          installPhase = "touch $out";
        };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.gcc-arm-embedded
            pkgs.gnumake
            pkgs.python3
            pkgs.python3Packages.pyusb
            pkgs.git
          ];
          shellHook = ''
            echo "t114-ch341: ./build.sh to build the UF2, (cd test && make) to run the protocol test"
          '';
        };
      });
    };
}
