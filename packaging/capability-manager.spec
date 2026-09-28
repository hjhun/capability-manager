# SPDX-License-Identifier: Apache-2.0
Name:           capability-manager
Version:        0.1.0
Release:        15
Summary:        Capability catalog and client library
License:        Apache-2.0 AND MIT
URL:            https://github.com/hjhun/capability-manager

%global capmgr_builddir _build
%global capmgr_libexecdir %{_libexecdir}/capmgr

Source0:        %{name}-%{version}.tar.gz
# nlohmann/json is MIT; its original license is installed with the runtime.
Source1:        https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  diffutils
BuildRequires:  python3
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(gtest)
BuildRequires:  pkgconfig(gmock)
%if 0%{?capmgr_tizen}
BuildRequires:  tidl
BuildRequires:  pkgconfig(rpc-port)
BuildRequires:  pkgconfig(bundle)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(cynara-client)
BuildRequires:  pkgconfig(cynara-creds-socket)
%endif

%description
Read-only capability catalog client. This initial development package fails
closed until platform authorization is integrated. It is not a complete platform
service deployment.

%package devel
Summary:        Capability Manager C API development files
Requires:       %{name} = %{version}-%{release}

%description devel
Public C header and link metadata; private writer headers are not installed.

%package offline-tools
Summary:        Explicit offline capability catalog transaction tool
Requires:       %{name} = %{version}-%{release}

%description offline-tools
Administrative staging and explicit outcome replay for an offline image catalog.
This is not a production installer finalizer or an automatically registered plugin.

%package tests
Summary:        Capability Manager unit and C ABI tests
Requires:       python3
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-offline-tools = %{version}-%{release}
%description tests
Real SQLite and mock-adapter unit tests plus a pure C consumer.

%prep
%setup -q
%{__tar} -xf %{SOURCE1}

%build
cmake -S . -B %{capmgr_builddir} -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
  -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
  -DCAPMGR_JSON_SOURCE=$PWD/json-3.11.3 -DBUILD_TESTING=ON \
  %{?capmgr_dependency_prefix:-DCMAKE_PREFIX_PATH=%{capmgr_dependency_prefix}} \
  -DCAPMGR_REQUIRE_TIDL=%{?capmgr_tizen:ON}%{!?capmgr_tizen:OFF} \
  -DCAPMGR_REQUIRE_CYNARA=%{?capmgr_tizen:ON}%{!?capmgr_tizen:OFF} \
  -DCAPMGR_BUILD_TIDL_TRANSPORT=%{?capmgr_tizen:ON}%{!?capmgr_tizen:OFF}
cmake --build %{capmgr_builddir} --parallel 1

%check
ctest --test-dir %{capmgr_builddir} --output-on-failure --no-tests=error

%install
DESTDIR=%{buildroot} cmake --install %{capmgr_builddir}
# Explicit fixture list; privileged probes are never run by package scripts.
for capmgr_test in \
    capmgr-unit-tests \
    capmgr-sqlite-lock-probe \
    capmgr-peer-tests \
    capmgr-adapter-tests \
    capmgr-cli-fixture \
    capmgr-c-consumer \
    capmgr-namespace-probe \
    capmgr-worker-probe \
    capmgr-bootstrap-context-probe \
    capmgr-bootstrap-image-fixture \
    capmgr-catalog-benchmark; do
  install -D -m 0755 %{capmgr_builddir}/$capmgr_test \
    %{buildroot}%{capmgr_libexecdir}/$capmgr_test
done
install -D -m 0755 %{capmgr_builddir}/capmgr-installed-bootstrap-probe \
  %{buildroot}%{capmgr_libexecdir}/capmgr-bootstrap-probe

%if 0%{?capmgr_tizen}
for capmgr_test in capmgr-tidl-probe capmgr-read-transport-probe; do
  install -D -m 0755 %{capmgr_builddir}/$capmgr_test \
    %{buildroot}%{capmgr_libexecdir}/$capmgr_test
done
install -D -m 0755 test/integration/run_tidl_probe.py \
  %{buildroot}%{capmgr_libexecdir}/run_tidl_probe.py
%endif

install -D -m 0755 test/integration/db_access_probe.py \
  %{buildroot}%{capmgr_libexecdir}/db_access_probe.py
install -D -m 0755 tools/verify.py %{buildroot}%{capmgr_libexecdir}/verify.py
install -D -m 0755 tools/run_bounded.py %{buildroot}%{capmgr_libexecdir}/run_bounded.py
install -D -m 0644 test/integration/package_tool_test.py \
  %{buildroot}%{capmgr_libexecdir}/package_tool_test.py
install -D -m 644 json-3.11.3/LICENSE.MIT %{buildroot}%{_datadir}/licenses/%{name}/nlohmann-json-LICENSE.MIT

%files
%license LICENSE
%{_libdir}/libcapmgr.so.0*
%{_datadir}/licenses/%{name}/nlohmann-json-LICENSE.MIT

%files devel
%{_libdir}/libcapmgr.so
%{_includedir}/capmgr/capmgr.h
%{_libdir}/pkgconfig/%{name}.pc

%files offline-tools
%{capmgr_libexecdir}/capmgr-package-tool

%files tests
%{capmgr_libexecdir}/capmgr-unit-tests
%{capmgr_libexecdir}/capmgr-sqlite-lock-probe
%{capmgr_libexecdir}/capmgr-peer-tests
%if 0%{?capmgr_tizen}
%{capmgr_libexecdir}/capmgr-tidl-probe
%{capmgr_libexecdir}/capmgr-read-transport-probe
%{capmgr_libexecdir}/run_tidl_probe.py
%endif
%{capmgr_libexecdir}/capmgr-c-consumer

%{capmgr_libexecdir}/capmgr-adapter-tests
%{capmgr_libexecdir}/capmgr-cli-fixture

%{capmgr_libexecdir}/capmgr-namespace-probe
%{capmgr_libexecdir}/capmgr-worker-probe
%{capmgr_libexecdir}/capmgr-bootstrap-probe
%{capmgr_libexecdir}/capmgr-bootstrap-context-probe
%{capmgr_libexecdir}/capmgr-bootstrap-image-fixture
%{capmgr_libexecdir}/capmgr-catalog-benchmark
%{capmgr_libexecdir}/verify.py
%{capmgr_libexecdir}/db_access_probe.py
%{capmgr_libexecdir}/run_bounded.py
%{capmgr_libexecdir}/package_tool_test.py
%doc tools/README.md
