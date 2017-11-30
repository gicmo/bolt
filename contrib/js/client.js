/*
 * Copyright © 2017 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const Lang = imports.lang;
const Signals = imports.signals;

/* Keep in sync with data/org.freedesktop.bolt.xml */

const BoltClientInterface = '<node> \
  <interface name="org.freedesktop.bolt1.Manager"> \
    <property name="Version" type="u" access="read"></property> \
    <property name="Probing" type="b" access="read"></property> \
    <method name="ListDevices"> \
      <arg name="devices" direction="out" type="ao"> </arg> \
    </method> \
    <method name="DeviceByUid"> \
      <arg type="s" name="uid" direction="in"> </arg> \
      <arg name="device" direction="out" type="o"> </arg> \
    </method> \
    <method name="EnrollDevice"> \
      <arg type="s" name="uid" direction="in"> </arg> \
      <arg type="u" name="policy" direction="in"> </arg> \
      <arg type="u" name="flags" direction="in"> </arg> \
      <arg name="device" direction="out" type="o"> </arg> \
    </method> \
    <method name="ForgetDevice">  \
      <arg type="s" name="uid" direction="in"> </arg> \
    </method> \
    <signal name="DeviceAdded"> \
      <arg name="device" type="o"> </arg> \
    </signal> \
    <signal name="DeviceRemoved"> \
      <arg name="device" type="o"> </arg> \
    </signal> \
  </interface> \
</node>';

const BoltDeviceInterface = '<node> \
  <interface name="org.freedesktop.bolt1.Device"> \
    <property name="Uid" type="s" access="read"></property> \
    <property name="Name" type="s" access="read"></property> \
    <property name="Vendor" type="s" access="read"></property> \
    <property name="Status" type="u" access="read"></property> \
    <property name="SysfsPath" type="s" access="read"></property> \
    <property name="Security" type="u" access="read"></property> \
    <property name="Parent" type="s" access="read"></property> \
    <property name="Stored" type="b" access="read"></property> \
    <property name="Policy" type="u" access="read"></property> \
    <property name="Key" type="u" access="read"></property> \
    <method name="Authorize"> \
      <arg type="u" name="flags" direction="in"> </arg> \
    </method> \
  </interface> \
</node>';

const BoltClientProxy = Gio.DBusProxy.makeProxyWrapper(BoltClientInterface);
const BoltDeviceProxy = Gio.DBusProxy.makeProxyWrapper(BoltDeviceInterface);

/*  */

var Status = {
    DISCONNECTED: 0,
    CONNECTED: 1,
    AUTHORIZING: 2,
    AUTH_ERROR: 3,
    AUTHORIZED: 4,
    AUTHORIZED_SECURE: 5,
    AUTHORIZED_NEWKY: 6
};

var Policy = {
    DEFAULT: 0,
    MANUAL: 1,
    AUTO:2
};

const BOLT_DBUS_NAME = 'org.freedesktop.bolt';
const BOLT_DBUS_PATH = '/org/freedesktop/bolt';

var Client = new Lang.Class({
    Name: 'BoltClient',

    _init: function(readyCallback) {
	this._readyCallback = readyCallback;

	new BoltClientProxy(
	    Gio.DBus.system,
	    BOLT_DBUS_NAME,
	    BOLT_DBUS_PATH,
	    Lang.bind(this, this._onProxyReady)
	);

	this._signals = [];

    },

    _onProxyReady: function(proxy, error) {
	if (error !== null) {
	    log(error.message);
	    return;
	}
	this._cli = proxy;
	let s = this._cli.connectSignal('DeviceAdded', Lang.bind(this, this._onDeviceAdded));
	this._signals.push(s);
	this._readyCallback(this);
    },

    _onDeviceAdded: function(proxy, emitter, params) {
	let [path] = params;
	let device = new BoltDeviceProxy(Gio.DBus.system,
					 BOLT_DBUS_NAME,
					 path);
	this.emit('device-added', device);
    },

    /* public methods */
    close: function() {
	while (this._signals.length) {
	    let sid = this._signals.shift();
	    this._cli.disconnectSignal(sid);
	}
	this._cli = null;
    },

    listDevices: function(callback) {
	this._cli.ListDevicesRemote(Lang.bind(this, function (res, error) {
	    if (error) {
		callback(null, error);
		return;
	    }

	    let [paths] = res;

	    let devices = [];
	    for (let i = 0; i < paths.length; i++) {
		let path = paths[i];
		let device = new BoltDeviceProxy(Gio.DBus.system,
						 BOLT_DBUS_NAME,
						 path);
		devices.push(device);
	    }
	    callback (devices, null);
	}));

    },

    enrollDevice: function(id, policy, callback) {
	this._cli.EnrollDeviceRemote(id, policy, Lang.bind(this, function (res, error) {
	    if (error) {
		callback(null, error);
		return;
	    }

	    let [path] = res;
	    let device = new BoltDeviceProxy(Gio.DBus.system,
					     BOLT_DBUS_NAME,
					     path);
	    callback(device, null);
	}));
    },

    deviceGetParent: function(dev, callback) {
	let parentUid = dev.Parent;

	if (!parentUid) {
	    callback (null, dev, null);
	    return;
	}

	this._cli.DeviceByUidRemote(dev.Parent, Lang.bind(this, function (res, error) {
	    if (error) {
		callback(null, dev, error);
		return;
	    }

	    let [path] = res;
	    let parent = new BoltDeviceProxy(Gio.DBus.system,
					     BOLT_DBUS_NAME,
					     path);
	    callback(parent, dev, null);
	}));
    },
});

Signals.addSignalMethods(Client.prototype);
