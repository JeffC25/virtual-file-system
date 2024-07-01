SRCDIR = src
BUILDDIR = build

all: $(BUILDDIR)/disk.o $(BUILDDIR)/fs.o

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/%.h | $(BUILDDIR)
	gcc -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
