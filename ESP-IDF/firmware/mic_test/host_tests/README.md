# Host HPF response check

Run outside ESP-IDF with a normal C compiler:

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -I../main ../main/hpf_filter.c hpf_response_test.c -lm -o /tmp/hpf_response_test
/tmp/hpf_response_test
```

The fixed-gain, rounding, and signed 24-bit saturation checks can be run with:

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -I../main audio_sample_math_test.c -o /tmp/audio_sample_math_test
/tmp/audio_sample_math_test
```
