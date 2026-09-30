'use strict';

// Ask Morgan before adding paths to this array. These files are
// intended to be only a subset of the G3D data that overlaps with the
// McGuire Computer Graphics Archive.
var meshSourcePathArray = [
    'research/model/bedroom',
    'research/model/bmw',
    'research/model/breakfast_room',
    'research/model/buddha',
    'research/model/bunny',
    'common/model/CornellBox',
    'research/model/cloud',
    'research/model/conference',
    'common/model/crytek_sponza',
    'common/model/cube',
    'research/model/dabrovic_sponza',
    'research/model/dragon',
    'research/model/erato',
    'research/model/fireplace_room',
    'research/model/gallery',
    'research/model/geodesic',
    'research/model/hairball',
    'common/model/holodeck',
    'research/model/horseChestnut',
    'research/model/indonesian',
    'research/model/lost_empire',
    'research/model/living_room',
    'research/model/lpshead',
    'common/model/mori_knob',
    'research/model/mitsuba_knob',
    'research/model/powerplant',
    'research/model/roadBike',
    'research/model/rungholt',
    'research/model/salle_de_bain',
    'research/model/San_Miguel',
    'research/model/scrubPine',
    'research/model/serapis',
    'research/model/sibenik',
    'research/model/sphere',
    'research/model/sportsCar',
    'common/model/teapot',
    'research/model/vokselia_spawn',
    'research/model/white_oak',
    'research/model/bistro',
];

/** An array of the data from the info.js files in paths
    located in the meshSourcePathArray. */
var meshArray = [];

/** Detect ancient browsers */
var isInternetExplorer = window.document.documentMode || false;
var isSafari = (navigator.userAgent.indexOf('Safari') != -1 && navigator.userAgent.indexOf('Chrome') == -1);


/** Derived from https://stackoverflow.com/questions/2339440/download-multiple-files-with-a-single-action */
function downloadMultipleFiles(files) {
    function download_next(i) {
        if (i >= files.length) { return; }
        
        var a = document.createElement('a');
        a.href = files[i].source;
        a.style.display = 'none';

        // If supported, set the destination filename
        if ('download' in a) { a.download = files[i].destination; }
        
        // Add a to the doc for click to work.
        (document.body || document.documentElement).appendChild(a);

        if (a.click) { a.click(); }

        // Delete the temporary link
        a.parentNode.removeChild(a);

        if (i < files.length + 1) {
            // Download the next file with a small timeout. The timeout is necessary
            // for IE, which will otherwise only download the first file, and for
            // Chrome, which limits the rate of downloading.
            setTimeout(function () { download_next(i + 1); },
                       (i === 9) ? 1000 :
                          (isInternetExplorer ? 500 : 1));
        }
    }
    
    // Initiate the first download.
    download_next(0);
}

/** Polyfill https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array/isArray */
if (! Array.isArray) { Array.isArray = function(arg) { return Object.prototype.toString.call(arg) === '[object Array]'; }; }

/** Returns {source:, destination:} 

    If the filename has the form "X as Y", then Y is used as the
    download name.
*/
function parseDownloadAlias(path, filename) {
    var result = filename.match(/(\S+)\s+as\s+(\S+)/);
    if (result) {
        return {source: path + '/' + result[1], destination: result[2]};
    } else {
        return {source: path + '/' + filename, destination: filename}
    }
}

/** 
    \param path The common server path to these files
    \param files One (a String) or more (Array of String) files relative to path to download

    All of the files are of the form {source:, destination:} from parseDownloadAlias()/

    Creates a function that downloads all of the files using downloadMultipleFiles().
*/
function makeDownloader(path, files) {
    var filenameArray = [], downloadFilenameArray = [];
    for (var i = 0; i < files.length; ++i) {
        filenameArray.push(files[i].source);
        downloadFilenameArray.push(files[i].destination);
    }

    return 
}

// Maps paths to downloader functions
var downloader = {};

function generateMeshEntries() {
    var index   = '';
    var content = '';
    for (var m = 0; m < meshArray.length; ++m) {
	var info = meshArray[m];

        // Ensure that we always have an array
        var files = info.downloadFilename;
        if (! Array.isArray(files)) {
            files = [files];
        }

        // Parse aliases
        for (var i = 0; i < files.length; ++i) {
            files[i] = parseDownloadAlias(info.path, files[i]);
        }

        // This weird construct using a function to create a scope
        // line is to capture the value of `files` on each iteration,
        // since the code is written for ancient browsers and can't
        // use `let`; `var` has function scope.
        downloader[info.path] =
            (function (files) {
                return function () { downloadMultipleFiles(files); };
            })(files);
        
        // Make the download tooltip show the filename, even though
        // we're going to override the actual link behavior
        var link = '<a href="' + files[0].source + '" onclick="event.preventDefault(), downloader[\'' + info.path + '\']()">';
        if (isSafari) {
            // The multi-file downloader doesn't work on Safari, so make a normal link to the
            // first file and hope there's only one.
            link = '<a href="' + files[0].source + '" download="' + files[0].destination + '">';
        }

	// Index entry
	index += '<a href="#mesh' + m + '"><img class="smallicon" src="' + info.path + '/icon.png" title="' + info.title + '"></a>';
	
	// Generate the entry for one mesh
	content += '<div class="item"><a name="mesh' + m + '"></a>' +
	    '<table><tbody><tr>' +
	    '<td class="iconcell">' + link + '<img class="icon" src="' + info.path + '/icon.png"></a>' +
	    '<div>' +
	    link + '<b>Download</b> ' + info.downloadSize + '</a>' +
	    '<br/><br/>Triangles: ' + info.triangles +
	    '<br/>Vertices: ' + info.vertices +
	    '<br/>Updated: ' + info.updatedDate +
	    '<br/>License: ' + info.license +
	    '<br/>' + info.copyright +
	    '</div></td>' +
	    '<td><h2 class="datatitle"><span></span>' + info.title + '</h2>' +
	    info.description + '</td>' +
	    '</tr></tbody></table></div>';
	// console.log(info.downloadFilename);
    }

    document.getElementById('meshIndex').innerHTML = index;
    document.getElementById('meshContent').innerHTML = content;
}


(function() {
    var html = '<script>var info;</script>';
    for (var i = 0; i < meshSourcePathArray.length; ++i) {
	var path = meshSourcePathArray[i];
	html += '<script src="' + path + '/info.js?"></script>' +
 	    '<script>info.path = "' + path + '"; meshArray.push(info);</script>';
    }

    html += '<script>generateMeshEntries();</script>';
    document.write(html);
})();



