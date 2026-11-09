// stampver.js - the vCPC's build number into src/cos_build.h, from docs/build-info.txt's top entry
// (YYYYMMDD-N, newest first): #define COS_BUILD "20261109-1". VER on a vCPC shows it (a slot reads
// it from its page zero, #0051: src/vcpc.c cos_version). Run by make and by src/build.bat before
// compiling; the header is kept in the tree too, so a compile by hand (gcc, cl) has the last one.
//   node tools/stampver.js
// (c) 2026 Cyborg Unicorn Pty Ltd - MIT License
var fs = require('fs');
var path = require('path');
var strRoot = path.resolve(__dirname, '..');
var strInfo = path.join(strRoot, 'docs', 'build-info.txt');
var strH = path.join(strRoot, 'src', 'cos_build.h');
var arrM = /^(\d{8}-\d+)\s*$/m.exec(fs.readFileSync(strInfo, 'utf8'));
var strText = '';

if (!arrM)
{
	console.error('stampver: no YYYYMMDD-N entry in ' + strInfo);
	process.exit(1);
}
strText = '/* cos_build.h - the build number VER shows on a vCPC: docs/build-info.txt\'s top entry. WRITTEN BY\n' +
	'   tools/stampver.js (make and src/build.bat run it): do not edit - add the build-info entry instead. */\n' +
	'#define COS_BUILD "' + arrM[1] + '"\n';
if (!fs.existsSync(strH) || fs.readFileSync(strH, 'utf8') !== strText)
{
	fs.writeFileSync(strH, strText);
	console.log('version ' + arrM[1] + ' -> src/cos_build.h');
}
