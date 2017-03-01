import sys

# Word boundary RE
wb = r'\([^[:alnum:]_"]\)'

f = open(sys.argv[1], 'r')
for sym in f:
  entry = sym.lstrip().rstrip()
  words = sym.split()
  sym = words[0]
  if len(words) == 1:
    if sym[0] == '_':
      newsym = '_k' + sym[1:]
    else:
      newsym = 'k' + sym
  else:
    newsym = words[1]

  spat = wb+sym+wb
  print "g/"+spat+"/s/"+spat+r"/\1"+newsym+r"\2/g"

  spat = '^'+sym+wb
  print "g/"+spat+"/s/"+spat+r"/"+newsym+r"\1/g"

  spat = wb+sym+'$'
  print "g/"+spat+"/s/"+spat+r"/\1"+newsym+r"/g"

  spat = '^'+sym+'$'
  print "g/"+spat+"/s/"+spat+r"/"+newsym+r"/g"

print "w"
print "q"
