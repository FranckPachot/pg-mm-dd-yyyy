FROM postgres:17-bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential postgresql-server-dev-17 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY Makefile mmddyyyy.control ./
COPY sql/ ./sql/
COPY src/ ./src/
RUN make \
    && make install DESTDIR=/install

FROM postgres:17-bookworm

COPY --from=build /install/usr/lib/postgresql/17/lib/mmddyyyy.so /usr/lib/postgresql/17/lib/
COPY --from=build /install/usr/share/postgresql/17/extension/ /usr/share/postgresql/17/extension/