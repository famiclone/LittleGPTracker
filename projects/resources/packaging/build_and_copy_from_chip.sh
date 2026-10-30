ssh chip@pocketchip.local 'cd /home/chip/Documents/SwineTracker/projects/ \
&& git checkout main && git pull && make'
scp chip@pocketchip.local:/home/chip/Documents/SwineTracker/projects/lgpt.rpi-exe $(git rev-parse --show-toplevel)/projects/lgpt.chip-exe
