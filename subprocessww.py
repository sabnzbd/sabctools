#!/usr/bin/python -OO
# Copyright 2008-2017 The SABnzbd-Team <team@sabnzbd.org>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# Patch all the functions and data from the custom function
import _subprocess
import _subprocessww
_subprocess.GetStdHandle = _subprocessww.GetStdHandle
_subprocess.GetCurrentProcess = _subprocessww.GetCurrentProcess
_subprocess.DuplicateHandle = _subprocessww.DuplicateHandle
_subprocess.CreateProcess = _subprocessww.CreateProcess
_subprocess.TerminateProcess = _subprocessww.TerminateProcess
_subprocess.GetExitCodeProcess = _subprocessww.GetExitCodeProcess
_subprocess.WaitForSingleObject = _subprocessww.WaitForSingleObject