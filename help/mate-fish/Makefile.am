@YELP_HELP_RULES@

HELP_ID = mate-fish

HELP_FILES = \
	fish-introduction.page \
	fish-settings.page \
	fish-usage.page \
	index.page \
	legal.xml

DUCK_FILES = $(wildcard C/*.duck)
PAGE_FILES = $(patsubst %.duck,%.page,$(DUCK_FILES))
$(PAGE_FILES): %.page: %.duck
	ducktype $^

HELP_MEDIA = figures/fish_applet.png

# Add linguas to be ignored, e.g. IGNORE_HELP_LINGUAS = ca de es fr
IGNORE_HELP_LINGUAS =
HELP_LINGUAS = $(if $(IGNORE_HELP_LINGUAS), \
	$(filter-out $(IGNORE_HELP_LINGUAS),$(subst /,,$(dir $(wildcard */*.po)))), \
	$(subst /,,$(dir $(wildcard */*.po))) )

-include $(top_srcdir)/git.mk
