RST2HTML = $(shell which rst2html || which rst2html.py)
RSTOPTS = --stylesheet-path=style.css,html4css1.css --initial-header-level=2

HTML = $(patsubst %.rst,%.html,$(wildcard *.rst))

.PHONY: clean

all : html

html : $(HTML)

%.html: %.rst style.css
	$(RST2HTML) $(RSTOPTS) $< $@

clean:
	rm -f $(HTML)
