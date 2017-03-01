import sys

# Word boundary RE
wb = r'\([^[:alnum:]_"]\)'

f = open(sys.argv[1], 'r')
for sym in f:
  sym = sym.lstrip().rstrip()
  if sym[0] == '_':
    newsym = '_k' + sym[1:]
  else:
    newsym = 'k' + sym

  spat = wb+sym+wb
  print r"g/"+spat+"/s/"+spat+r"/\1"+newsym+r"\2/g"
  spat = '^'+sym+wb
  print r"g/"+spat+"/s/"+spat+r"/\1"+newsym+r"\2/g"
  spat = wb+sym+'$'
  print r"g/"+spat+"/s/"+spat+r"/\1"+newsym+r"\2/g"
  spat = '^'+sym+'$'
  print r"g/"+spat+"/s/"+spat+r"/\1"+newsym+r"\2/g"

print "w"
print "q"
