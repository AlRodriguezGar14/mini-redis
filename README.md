### This is a mini-recreation of Redis in C++. It is a solution to the CodeCrafters.io challenge Build Your Own Redis.

This servers connects with the redis-cli and can handle the following commands: PING, ECHO, GET, SET (with expiration time) - more are to be added in the future.
It also parses the Redis protocol and can handle multiple clients at the same time using a single threaded event loop (epoll) so that it is closer to the original solution without threads.

Disclaimer: I am not responsible for any misuse of this code. This code is intended for educational purposes only.

[![progress-banner](https://backend.codecrafters.io/progress/redis/cc8e9821-f1cb-4ee2-9c5e-2992d21f3794)](https://app.codecrafters.io/users/AlRodriguezGar14?r=2qF)
