RSTCSS = $(shell python -c 'import docutils.writers.html4css1 as m; print m.Writer.default_stylesheet_path')
RSTOPTS = --stylesheet-path=style.css,$(RSTCSS) --initial-header-level=2

HTML = $(patsubst %.rst,%.html,$(wildcard *.rst))

.PHONY: clean

all : html

html : $(HTML)

%.html: %.rst style.css
	rst2html $(RSTOPTS) $< $@

clean:
	rm -f $(HTML)
