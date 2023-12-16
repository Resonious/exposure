{ pkgs ? import <nixpkgs> {} }:

let
  # Change the version number here if we upgrade Ruby
  ruby = pkgs.buildPackages.ruby_3_2;

in pkgs.mkShell {
  nativeBuildInputs = with pkgs.buildPackages; [
    ruby
    gcc
    openssl
    curl
    git
    which
    jq
  ];
}
