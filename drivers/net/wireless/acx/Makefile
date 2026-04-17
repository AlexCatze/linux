#                                                       -*-Makefile-*-
# Copyright (C) 2008
# The ACX100 Open Source Project <acx100-devel@lists.sourceforge.net>
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# 
# This makefile, allows compiling driver either inside or outside the kernel
# tree without further modifications.
#
# Please read the INSTALL file for further information.


obj-$(CONFIG_ACX_MAC80211) += acx-mac80211.o
acx-mac80211-obj-$(CONFIG_ACX_MAC80211_PCI) += pci.o
acx-mac80211-obj-$(CONFIG_ACX_MAC80211_USB) += usb.o
acx-mac80211-obj-$(CONFIG_ACX_MAC80211_MEM) += mem.o
acx-mac80211-objs := common.o $(acx-mac80211-obj-y)

obj-$(CONFIG_HX4700_ACX)        += hx4700_acx.o
obj-$(CONFIG_RX1950_ACX)        += rx1950_acx.o
