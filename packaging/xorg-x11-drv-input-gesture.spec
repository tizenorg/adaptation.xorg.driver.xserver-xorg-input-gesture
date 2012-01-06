Name:       xorg-x11-drv-input-gesture
Summary:    X.Org X server -- Xserver gesture driver
Version:    0.1.0
Release:    4
Group:      System/X Hardware Support
License:    MIT
Source0:    %{name}-%{version}.tar.gz
Requires:   xorg-x11-server
BuildRequires:  pkgconfig(xorg-server)
BuildRequires:  pkgconfig(gestureproto)
BuildRequires:  pkgconfig(xproto)
BuildRequires:  pkgconfig(inputproto)
%description
 This package provides the driver for recognizing gesture(s) using button
and motion events inside X server.


%package devel
Summary:    Development files for xorg gesture driver
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
xorg-x11-drv-input-gesture development files


%prep
%setup -q 

%build

autoreconf -vfi
./configure --prefix=/usr --mandir=/usr/share/man --infodir=/usr/share/info CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS"

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%remove_docs

%files
%{_libdir}/xorg/modules/input/gesture_drv.so

%files devel
%{_libdir}/pkgconfig/xorg-gesture.pc
