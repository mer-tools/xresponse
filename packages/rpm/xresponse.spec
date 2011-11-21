Name: xresponse
Version: 1.2
Release: 1%{?dist}
Summary: X11 debug tool
Group: Development/Tools
License: GPLv2+
URL: http://www.gitorious.org/+maemo-tools-developers/maemo-tools/xresponse
Source: %{name}_%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
BuildRequires: autoconf, automake, pkg-config, xorg-x11-devel xorg-x11-libX11-devel, glib2-devel

%description
 xresponse is a simple tool for meassuring UI response times to a full mouse
 click event. It requires the Xtest, to 'fake' the mouse event, and XDamage, to
 report areas of the display that have changed.
 
%prep
%setup -q -n xresponse

%build
autoreconf -fvi

%configure

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/xresponse
%{_mandir}/man1/xresponse.1.gz
%doc README COPYING 


%changelog
* Tue Oct 25 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2
  * Add preconfigurable default emulation device.

* Fri Sep 23 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.1.1
  * Remove event processing before click and key emulation. Fixes xresponse
    getting stuck processing continous stream of damage events for some
    applications.

* Tue Aug 23 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.1
  * Add 'support' for multi axes pointing devices

* Thu May 05 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.0.2
  * Fix "xresponse --click" crashes

* Thu Apr 07 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.0.1
  * Fix output issues with xresponse response reporting

* Thu Feb 24 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.0
  * Rewrite/split xresponse.c to separate files and change it to
    use Glib for data structures
  * Fix where xresponse drag doesn't work to minimize applications

* Mon Sep 27 2010 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-25
  * Fix xresponse aborting to Bad Damage error

* Fri Sep 10 2010 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-24
  * Fix xresponse keypress emulation delay not working

* Thu Oct 15 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-23
  * Monitor root window by default

* Thu Sep 28 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-22
  * Fix drag option failure in browser as it cannot handle pressure events
  * Fix more problems with drag option
  * Fix events reported in wrong order
  * Fix user input emulation delay options blocking incoming damage events

* Wed Sep 23 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-21
  * Fix reports incorrect response times with the -r option

* Fri Sep 04 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-20
  * Fix xresponse memory corruption

* Thu Jun 18 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-19
  * Add an option to monitor all applications
  * Add new option to monitor application response times to user actions

* Tue Jun 16 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-18
  * Flush the output

* Mon May 25 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-17
  * Fix incorrect timestamp difference reported for click emulation

* Tue May 19 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-16
  * Damage and input events might not be reported in correct order fix

* Wed Apr 22 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-15
  * Add an option to specify delay between drag events
  * Xresponse does not warn about overriding wait values

* Fri Mar 13 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-14
  * Fix -m option using relative damage position instead of
    translating it to the absolute (screen) position
  * Added another parameter to the -b option
  * Added window creating/mapping/unmapping reporting 
  * Moved mouse/keyboard input emulation after 
    initializing window/application monitoring
  * Added option to monitor user input

* Wed Feb 25 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-13
  * Added new commands: --application, --exclude, --break, --level.
  * Changed command processing. Instead of monitoring damage after every
    mouse/keyboard command the damage is monitored after all commands
    are processed. This means that the previously used command
    sequences, like: xresponse -w 1 -c 10x10 -c 20x20 -w 5 -c 30x30 must
    now be split over multiple xresponse calls.

* Mon Feb 23 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-12
  * Improved building keycode-keysyms mapping, which is used to emulate
    text string typing (-t option). 
  * Fixed crash when the string passed with -t option contained
    unrecognized symbols

* Mon Feb 16 2009 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-11
  * Added an optional delay parameter to -c option to control time
    between mousebutton press and release events. 
  * Added libxi-dev as build dependency

* Tue Oct 21 2008 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-10
  * Added -id option to allow monitoring damage events for other windows
    than root window as well. 

* Wed Jun 25 2008 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-9
  * Documentation improvements.
  * The -k option now supports default delay between artifical
    keypress/-release events. 

* Thu Mar 06 2008 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-8
  * Logging improved.
  * Monitor damage events also for synthesized key events.

* Tue Jan 22 2008 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-7
  * Imported some code from xautomation to xresponse in order to
    implement support for synthesizing keyboard events.

* Wed Sep 12 2007 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-6
  * Changed the way the overlap between monitored area and damage events
    is checked

* Wed Jun 27 2007 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-5
  * correct copyright information
  * proper man pages

* Tue Aug 29 2006 Eero Tamminen <eero.tamminen@nokia.com> 0.3.2-3
  * Fix xresponse waiting forever if event received at timeout
