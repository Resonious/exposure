{ pkgs ? import <nixpkgs> {} }:

let
  clang = pkgs.llvmPackages_17.clang-unwrapped;
  # Change the version number here if we upgrade Ruby
  ruby = pkgs.buildPackages.ruby_3_2;
  rubySrc =
    with import <nixpkgs> {};
    stdenv.mkDerivation {
      name = "ruby-3-2-source";

      src = fetchFromGitHub {
        owner = "ruby";
        repo = "ruby";
        rev = "7b05cb8dbbd637910757f402e64da3908b4bb809";
        sha256 = "sha256-gyFFgd1y65Wmj+qHiPIFvjHmNpsWij8l8bhkO7oLWvI=";
      };

      dontConfigure = true;
      dontBuild = true;

      installPhase = ''
        mkdir -p $out

        # Just copy over all Ruby includes
        cp -r include $out/include

        # Pull config.h from built Ruby
        rubyinclude="${pkgs.buildPackages.ruby_3_2}/include"
        ruby_h="$rubyinclude/$(ls "$rubyinclude" | head -n1)"
        ruby_config_h="$ruby_h/$(ls $ruby_h | grep -v ruby | head -n1)"
        cp "$ruby_config_h/ruby/config.h" "$out/include/ruby/config.h"

        # This is just freaky, but clangd will not use -I directives for quote includes,
        # so we need to symlink the heck out of the ruby directory.
        ln -s "$out/include/ruby" "$out/include/ruby/ruby"
        ln -s "$out/include/ruby" "$out/include/ruby/internal"
        ln -s "$out/include/ruby" "$out/include/ruby/internal/compiler_is"
      '';
    };

in pkgs.mkShell {
  nativeBuildInputs = with pkgs.buildPackages; [
    ruby
    rubySrc
    gcc
    clang
    openssl
    curl
    git
    which
    jq
  ];

  shellHook = ''
    clangflags() {
      echo
    }

    cat << YML > .clangd
CompileFlags:
  Add:
    - '-I${rubySrc}/include'
    - '-I${pkgs.glibc.dev}/include'
    - '-I${pkgs.lib.makeLibraryPath [ clang ]}/clang/17/include'
YML
  '';
}
