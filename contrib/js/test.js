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

imports.searchPath.unshift('.');
const Bolt = imports.client;

let gotParent = function (p, d, e) {
    if (e) {
	log(e);
	return;
    }

    if (!p) {
	print (' device: '+ d.Uid +  ' -> no parent');
	return;
    }

    print (' device: '+ d.Uid +  ' -> parent: ' + p.Uid);

};

let client = new Bolt.Client(function (client) {
    client.listDevices(function (devices, error) {
	if (error) {
	    print ('error ' + error);
	    return;
	}

	for (let i = 0; i < devices.length; i++) {
	    let d = devices[i];
	    print(' ' + d.Uid + " " + d.Name);

	    client.deviceGetParent(d, gotParent);
	}
    });
});

let loop = new GLib.MainLoop(null, false);
loop.run();
