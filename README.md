# gltf2png

Render 3D models (gltf & glb) to png images in the server / headless.
This program uses [Filament](https://google.github.io/filament/)

## Usage:

xvfb-run -a ./gltf2png model.glb 800x600 output.png

## Installation

OS: Debian 12 

Install the followin packages:

Install Filament version v1.56.5 https://github.com/google/filament/releases/download/v1.56.5/filament-v1.56.5-linux.tgz
Later versions won´t work in debian 12.
Extract the file and place the content in the same folder.