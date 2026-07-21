FROM ubuntu:22.04 AS build

LABEL maintainer="DavveDP"
LABEL version="0.1"
LABEL description="docker image for building hg-engine"

# The repo is bind-mounted to /hg-engine at run time, so we do NOT copy it into
# the image. This keeps the build context tiny and avoids copying local junk such
# as a broken .venv symlink or the multi-GB build/base/.git directories.
RUN DEBIAN_FRONTEND=noninteractive apt-get update -y && DEBIAN_FRONTEND=noninteractive apt-get upgrade -y \
&& DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends libpng-dev build-essential cmake python3-pip python3-venv git automake autoconf gcc-arm-none-eabi pkg-config -y \
&& apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 3FA7E0328081BFF6A14DA29AA6A19B38D3D831EF \
&& apt-get update -y \
&& ln -s /proc/self/mounts /etc/mtab || true

# Install the Python build dependency (ndspy) into the image now, while the
# docker build has network access. This lets `make` run fully offline at
# container run time (the Makefile skips its venv/pip step when ndspy is already
# importable), avoiding pypi timeouts if the running container has no internet.
RUN python3 -m pip install --no-cache-dir ndspy==4.1.0

WORKDIR /hg-engine
CMD ["/bin/bash", "-lc", "make -j$(nproc)"]
