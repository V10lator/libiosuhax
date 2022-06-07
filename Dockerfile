FROM wiiuenv/devkitppc:20220605

WORKDIR wut_update
RUN git clone -b 4NUSspli --single-branch https://github.com/V10lator/wut && \
 cd wut && \
 make -j$(nproc) && \
 make install && \
 cd .. && \
 rm -rf wut

WORKDIR tmp_build
COPY . .
FROM wut_update
COPY /opt/devkitpro/wut /opt/devkitpro/wut
RUN make clean && make && mkdir -p /artifacts/wut/usr && cp -r lib /artifacts/wut/usr && cp -r include /artifacts/wut/usr
WORKDIR /artifacts

FROM scratch
COPY --from=0 /artifacts /artifacts
