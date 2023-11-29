# iokerneld
sudo caladan/iokerneld ias
# ctrl
sudo stdbuf -o0 sh -c "bin/ctrl_main"

# main server
sudo stdbuf -o0 sh -c "ulimit -c unlimited; bin/test_cereal -m -l 1 -i 18.18.1.2"
# server
sudo stdbuf -o0 sh -c "ulimit -c unlimited; bin/test_cereal -l 1 -i 18.18.1.3"