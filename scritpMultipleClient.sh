#!/bin/bash

counter=1
cap_prime_num=()
cap_word_count=()
cap_vect_mult=()
str=()
while [ $counter -le 10 ]
do
    cap_prime_num+=($((100 + $RANDOM % 200)))
    cap_word_count+=($((1000 + $RANDOM % 3000)))
    cap_vect_mult+=($((1000 + $RANDOM % 1000)))
    str+=($"dev${counter-1}")
    ((counter++))
done

function killProcess {
    pid=$(ps ax | grep "MultipleClient.py" | awk '{print $1}')
    sudo kill $pid
}

trap killProcess EXIT
python3 MultipleClient.py ${str[0]} ${cap_prime_num[0]} ${cap_word_count[0]} ${cap_vect_mult[0]} & 
sleep .3 &
python3 MultipleClient.py ${str[1]} ${cap_prime_num[1]} ${cap_word_count[1]} ${cap_vect_mult[1]} & 
sleep .3 &
python3 MultipleClient.py ${str[2]} ${cap_prime_num[2]} ${cap_word_count[2]} ${cap_vect_mult[2]} & 
sleep .3 &
python3 MultipleClient.py ${str[3]} ${cap_prime_num[3]} ${cap_word_count[3]} ${cap_vect_mult[3]} & 
sleep .3 &
python3 MultipleClient.py ${str[4]} ${cap_prime_num[4]} ${cap_word_count[4]} ${cap_vect_mult[4]} & 
sleep .3 &
python3 MultipleClient.py ${str[5]} ${cap_prime_num[5]} ${cap_word_count[5]} ${cap_vect_mult[5]} & 
sleep .3 &
python3 MultipleClient.py ${str[6]} ${cap_prime_num[6]} ${cap_word_count[6]} ${cap_vect_mult[6]} & 
sleep .3 &
python3 MultipleClient.py ${str[7]} ${cap_prime_num[7]} ${cap_word_count[7]} ${cap_vect_mult[7]} & 
sleep .3 &
python3 MultipleClient.py ${str[8]} ${cap_prime_num[8]} ${cap_word_count[8]} ${cap_vect_mult[8]} & 
sleep .3 &
python3 MultipleClient.py ${str[9]} ${cap_prime_num[9]} ${cap_word_count[9]} ${cap_vect_mult[9]}



