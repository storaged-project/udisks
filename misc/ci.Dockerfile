# latest fedora image for running some of our tests in GH actions

FROM registry.fedoraproject.org/fedora:latest

RUN set -e; \
  dnf install -y ansible python3-pip rpm-build mock csmock git; \
  pip3 install git+https://github.com/vojtechtrefny/copr-builder.git; \
  git clone --depth 1 https://github.com/storaged-project/ci.git;

WORKDIR /
