ssh pi@retropie.local 'cd /home/pi/RetroPie/roms/ports/SwineTracker-FeatureImprovements/projects/ \
&& git checkout main && git pull && make PLATFORM=RASPI'
scp pi@retropie.local:/home/pi/RetroPie/roms/ports/SwineTracker-FeatureImprovements/projects/lgpt.rpi-exe $(git rev-parse --show-toplevel)/projects/
