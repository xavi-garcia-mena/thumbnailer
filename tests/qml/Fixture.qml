/*
 * Copyright (C) 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: James Henstridge <james.henstridge@canonical.com>
 */

import QtQuick 2.0
import QtTest 1.0
import Ubuntu.Thumbnailer 0.1
import testconfig 1.0

TestCase {
    id: root
    when: windowShown

    width: image.width > 0 ? image.width : 100
    height: image.height > 0 ? image.height : 100

    readonly property size size: Qt.size(image.implicitWidth, image.implicitHeight)
    property size requestedSize: Qt.size(1920, 1920)

    // This is depending on a private member, but having separate
    // dumps for each test case is quite useful.
    property string dumpfile: "%1/qml/dump-%2.png".arg(Config.buildDir).arg(qtest_results.functionName)
    property variant imageData: null

    Image {
        id: image
        width: implicitWidth
        height: implicitHeight
        sourceSize: root.requestedSize

        signal grabFinished

        SignalSpy {
            id: statusSpy
            target: image
            signalName: "statusChanged"
        }

        SignalSpy {
            id: grabSpy
            target: image
            signalName: "grabFinished"
        }
    }

    Canvas {
        id: canvas
        contextType: "2d"

        SignalSpy {
            id: loadSpy
            target: canvas
            signalName: "imageLoaded"
        }
    }

    function loadThumbnail(filename) {
        var url = Qt.resolvedUrl(Config.mediaDir + "/" + filename);
        load("image://thumbnailer/" + url);
    }

    function loadAlbumArt(artist, album) {
        load("image://albumart/artist=" +
                   encodeURIComponent(artist) +
                   "&album=" +
                   encodeURIComponent(album));
    }

    function loadArtistArt(artist, album) {
        load("image://artistart/artist=" +
                   encodeURIComponent(artist) +
                   "&album=" +
                   encodeURIComponent(album));
    }

    function load(url) {
        // Have the image component load the image
        image.source = url;
        while (image.status === Image.Loading) {
            statusSpy.wait();
        }
        compare(image.status, Image.Ready);

        // Grab the image component contents to the dump file
        grabSpy.clear()
        compare(image.grabToImage(function(result) {
            result.saveToFile(dumpfile);
            image.grabFinished();
        }), true);
        if (grabSpy.count == 0) {
            grabSpy.wait();
        }

        // Have the canvas load up the image data
        canvas.unloadImage(dumpfile);
        canvas.loadImage(dumpfile);
        while (canvas.isImageLoading(dumpfile)) {
            loadSpy.wait();
        }
        compare(canvas.isImageError(dumpfile), false);

        // And finally read in the image data
        imageData = canvas.context.createImageData(dumpfile);
    }

    function expectLoadError(url) {
        image.source = url;
        while (image.status === Image.Loading) {
            statusSpy.wait();
        }
        compare(image.status, Image.Error);
    }

    function pixel(x, y) {
        var pos = (imageData.width * y + x) * 4;
        var data = imageData.data;
        return Qt.rgba(data[pos] / 255, data[pos+1] / 255, data[pos+2] / 255, data[pos+3] / 255);
    }

    function comparePixel(x, y, expected) {
        var actual = pixel(x, y);
        if (!Qt.colorEqual(actual, expected)) {
            fail("Unexpected pixel value at (" + x + ", " + y + ")\n" +
                 "actual:   " + actual + "\nexpected: " + expected);
        }
    }

    function cleanup() {
        image.source = "";
        root.requestedSize = Qt.size(1920, 1920)
        while (image.status !== Image.Null) {
            spy.wait();
        }
    }
}
