for f in `cat fix/files`; do
  echo $f
  ed $f < fix/subst
done
