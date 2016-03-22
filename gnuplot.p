# vim: set filetype=gnuplot :
set term png size 600, 400
set output "result.png"
plot "result.dat" using 1:2 title "select" with linespoints, \
     "result.dat" using 1:3 title "poll" with linespoints, \
     "result.dat" using 1:4 title "epoll" with linespoints
