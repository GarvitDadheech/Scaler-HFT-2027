CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
TARGET = order_book_trader
SOURCES = main.cpp order_book.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
