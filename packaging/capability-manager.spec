# SPDX-License-Identifier: Apache-2.0
Name: capability-manager
Version: 0.1.0
Release: 9
Summary: Capability catalog and client library
License: Apache-2.0 AND MIT
URL: https://github.com/hjhun/capability-manager
Source0: %{name}-%{version}.tar.gz
# nlohmann/json is MIT; its original license is installed with the runtime.
Source1: https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
BuildRequires: cmake
BuildRequires: gcc-c++
BuildRequires: diffutils
BuildRequires: python3
BuildRequires: pkgconfig(sqlite3)
BuildRequires: pkgconfig(gtest)
BuildRequires: pkgconfig(gmock)
%if 0%{?capmgr_tizen}
BuildRequires: tidl
BuildRequires: pkgconfig(rpc-port)
BuildRequires: pkgconfig(bundle)
BuildRequires: pkgconfig(dlog)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(cynara-client)
BuildRequires: pkgconfig(cynara-creds-socket)
%endif

%description
Read-only capability catalog client. This initial development package fails
closed until platform authorization is integrated. It is not a complete platform
service deployment.

%package devel
Summary: Capability Manager C API development files
Requires: %{name} = %{version}-%{release}
%description devel
Public C header and link metadata; private writer headers are not installed.

%package offline-tools
Summary: Explicit offline capability catalog transaction tool
Requires: %{name} = %{version}-%{release}
%description offline-tools
Administrative staging and explicit outcome replay for an offline image catalog.
This is not a production installer finalizer or an automatically registered plugin.

%package tests
Summary: Capability Manager unit and C ABI tests
Requires: python3
Requires: %{name} = %{version}-%{release}
Requires: %{name}-offline-tools = %{version}-%{release}
%description tests
Real SQLite and mock-adapter unit tests plus a pure C consumer.

%prep
%setup -q
%{__tar} -xf %{SOURCE1}

%build
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
  -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
  -DCAPMGR_JSON_SOURCE=$PWD/json-3.11.3 -DBUILD_TESTING=ON \
  %{?capmgr_dependency_prefix:-DCMAKE_PREFIX_PATH=%{capmgr_dependency_prefix}} \
  -DCAPMGR_REQUIRE_TIDL=%{?capmgr_tizen:ON}%{!?capmgr_tizen:OFF} \
  -DCAPMGR_REQUIRE_CYNARA=%{?capmgr_tizen:ON}%{!?capmgr_tizen:OFF} \
  -DCAPMGR_BUILD_TIDL_TRANSPORT=%{?capmgr_tizen:ON}%{!?capmgr_tizen:OFF}
cmake --build _build --parallel 1

%check
ctest --test-dir _build --output-on-failure --no-tests=error

%install
DESTDIR=%{buildroot} cmake --install _build
install -D -m 755 _build/capmgr-unit-tests %{buildroot}%{_libexecdir}/capmgr/capmgr-unit-tests
install -D -m 755 _build/capmgr-peer-tests %{buildroot}%{_libexecdir}/capmgr/capmgr-peer-tests
%if 0%{?capmgr_tizen}
install -D -m 755 _build/capmgr-tidl-probe %{buildroot}%{_libexecdir}/capmgr/capmgr-tidl-probe
install -D -m 755 test/integration/run_tidl_probe.py %{buildroot}%{_libexecdir}/capmgr/run_tidl_probe.py
%endif
install -D -m 755 _build/capmgr-namespace-probe %{buildroot}%{_libexecdir}/capmgr/capmgr-namespace-probe
install -D -m 755 _build/capmgr-worker-probe %{buildroot}%{_libexecdir}/capmgr/capmgr-worker-probe
install -D -m 755 _build/capmgr-catalog-benchmark %{buildroot}%{_libexecdir}/capmgr/capmgr-catalog-benchmark
install -D -m 755 test/integration/db_access_probe.py %{buildroot}%{_libexecdir}/capmgr/db_access_probe.py
install -D -m 755 tools/verify.py %{buildroot}%{_libexecdir}/capmgr/verify.py
install -D -m 755 tools/run_bounded.py %{buildroot}%{_libexecdir}/capmgr/run_bounded.py
install -D -m 644 test/integration/package_tool_test.py %{buildroot}%{_libexecdir}/capmgr/package_tool_test.py
install -D -m 755 _build/capmgr-adapter-tests %{buildroot}%{_libexecdir}/capmgr/capmgr-adapter-tests
install -D -m 755 _build/capmgr-cli-fixture %{buildroot}%{_libexecdir}/capmgr/capmgr-cli-fixture
install -D -m 755 _build/capmgr-c-consumer %{buildroot}%{_libexecdir}/capmgr/capmgr-c-consumer
install -D -m 644 json-3.11.3/LICENSE.MIT %{buildroot}%{_datadir}/licenses/%{name}/nlohmann-json-LICENSE.MIT

%files
%license LICENSE
%{_libdir}/libcapmgr.so.0*
%{_datadir}/licenses/%{name}/nlohmann-json-LICENSE.MIT

%files devel
%{_libdir}/libcapmgr.so
%{_includedir}/capmgr/capmgr.h
%{_libdir}/pkgconfig/capmgr.pc

%files offline-tools
%{_libexecdir}/capmgr/capmgr-package-tool

%files tests
%{_libexecdir}/capmgr/capmgr-unit-tests
%{_libexecdir}/capmgr/capmgr-peer-tests
%if 0%{?capmgr_tizen}
%{_libexecdir}/capmgr/capmgr-tidl-probe
%{_libexecdir}/capmgr/run_tidl_probe.py
%endif
%{_libexecdir}/capmgr/capmgr-c-consumer

%{_libexecdir}/capmgr/capmgr-adapter-tests
%{_libexecdir}/capmgr/capmgr-cli-fixture

%{_libexecdir}/capmgr/capmgr-namespace-probe
%{_libexecdir}/capmgr/capmgr-worker-probe
%{_libexecdir}/capmgr/capmgr-catalog-benchmark
%{_libexecdir}/capmgr/verify.py
%{_libexecdir}/capmgr/db_access_probe.py
%{_libexecdir}/capmgr/run_bounded.py
%{_libexecdir}/capmgr/package_tool_test.py
%doc tools/README.md
