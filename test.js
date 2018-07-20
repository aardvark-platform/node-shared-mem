var tape = require('tape')
var n = require('./build/Release/node-shared-mem')

function str(buffer, len) {
	var a = new Uint8Array(buffer);
	var i = 0;
	var res = "";
	while(a[i] != 0 && i < len) {
		res += String.fromCharCode(a[i]);
		i++;
	}
	return res;
}

tape('does not crash', function (t) {
  var len = 1 << 20;
  var a = new n.SharedMemory('testfile', len)
  console.log("name:    " + a.name)
  console.log("length:  " + a.length)
  console.log("content: " + str(a.buffer,len))
  a.close()
  
  console.log("name:    " + a.name)
  console.log("length:  " + a.length)
  console.log("content: " + a.buffer)
  
  
  t.pass('did not crash')
  t.end()
})
