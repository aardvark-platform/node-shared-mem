{
  "targets": [{
    "target_name": "node-shared-mem",
    "sources": [
      "node-shared-mem.cc",
	  "addon.cc"
    ],
    "include_dirs": [
      "<!(node -e \"require('nan')\")",
    ]
  }]
}
