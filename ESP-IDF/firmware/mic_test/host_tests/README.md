# Host HPF response check

Run outside ESP-IDF with a normal C compiler:

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -I../main ../main/hpf_filter.c hpf_response_test.c -lm -o /tmp/hpf_response_test
/tmp/hpf_response_test
```
