Name: xresponse
Version: 1.2
Release: 1%{?dist}
Summary: x11 debug tool
Group: Development/Tools
License: GPLv2+
URL: http://www.gitorious.org/+maemo-tools-developers/maemo-tools/xresponse
Source: %{name}_%{version}.tar.gz
BuildRoot: {_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: autoconf, automake, pkg-config, xorg-x11-devel xorg-x11-libX11-devel

%description
 xresponse is a simple tool for meassuring UI response times to a full mouse
 click event. It requires the Xtest, to 'fake' the mouse event, and XDamage, to
 report areas of the display that have changed.
 
%prep
%setup -q -n xresponse

%build
autoreconf -fvi

%configure 
make 

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(755,root,root,-)
%{_bindir}/xresponse
%defattr(644,root,root,-)
%{_mandir}/man1/xresponse.1.gz
%doc README COPYING 

