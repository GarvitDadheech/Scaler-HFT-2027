class Node{
public:
	int val;
	Node *next;
};
class LinkedList{
public:
	LinkedList();
	void addToFront(int);
	void addToEnd(int);
private:
	Node * head;
};


