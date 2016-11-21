# Build Image:
# docker build -t dwarf-therapist .
#
# Run Container to build Dwarf-Therapist:
# docker run --rm -it -v `pwd`:/dt dwarf-therapist
# cd /dt
# cmake .
# make -j$(nproc)
#
# Install:
# Copy over `DwarfTherapist` and `share` folder and adjust permissions

FROM ubuntu:xenial

RUN apt-get update && apt-get install -y --no-install-recommends \
       qt5-qmake \
       qtbase5-dev \
       qtbase5-dev-tools \
       qtdeclarative5-dev \
       build-essential \
       cmake && \
    rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["/bin/bash"]
