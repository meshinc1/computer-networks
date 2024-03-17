CC = g++

CFLAGS = -g -Wall

SENDER = rSender

RECEIVER = rReceiver

all: $(SENDER) $(RECEIVER)

$(SENDER): $(SENDER).cpp
	$(CC) $(CFLAGS) -o $(SENDER) $(SENDER).cpp

$(RECEIVER): $(RECEIVER).cpp
	$(CC) $(CFLAGS) -o $(RECEIVER) $(RECEIVER).cpp

clean:
	$(RM) $(SENDER) $(RECEIVER)
