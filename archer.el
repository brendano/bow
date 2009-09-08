

;;; Buffers
(defvar archer-scratch-buffer "*Archer scratch*"
  "Process buffer for the TCP connection to Archer")
(defvar archer-dump-buffer "*Archer*"
  "Buffer in which matching files are displayed")
(defvar archer-hit-buffer "*Archer hits*"
  "Buffer in which hit list is displayed")
(defvar archer-fields-buffer "*Archer fields*"
  "Buffer for displaying list of fields available for query")

;;; Keymaps
(defvar archer-hit-map nil "Keymap for archer-hit-buffer")
(defvar archer-dump-map nil "Keymap for archer-dump-buffer")

;;; Parameters
(defvar archer-max-hits 20 
  "Maximum number of hits to show (setting to 0 removes limit); 
use archer-set-max-hits to set")
(defvar archer-summary-fields nil
  "Summary fields to use in displaying hits in hit buffer;
use archer-insert-summary-field and archer-delete-summary-field
to add and remove fields")

;;; Miscellaneous global variables
(defvar archer-server nil 
  "Name of Archer server host")
(defvar archer-port nil 
  "TCP port of Archer server")
(defvar archer-process-name "Archer" 
  "Process name for TCP connection")
(defvar archer-process nil 
  "Emacs process for TCP connection")
(defvar archer-last-query nil 
  "Last query issued by user")
(defvar archer-hits nil
  "List of hits from last query")
(defvar archer-hit-count nil
  "Number of matching documents")
(defvar archer-matcher-positions nil 
  "List of markers to matching terms in document in dump buffer")
(defvar archer-matcher-shown nil
  "Index of current matching term")
(defvar archer-prefix-length nil 
  "Length of prefix common to all file names (currently not used)")
(defvar archer-available-summary-fields nil 
  "List of summary fields used by serer")

;;; The archer highlight face
(copy-face 'default 'archer-highlight-face)
(set-face-foreground 'archer-highlight-face "red")



;;; ---------------- Stopword list --------------------

(defvar archer-stopwords-list
  '(
    "a"
    "able"
    "about"
    "above"
    "according"
    "accordingly"
    "across"
    "actually"
    "after"
    "afterwards"
    "again"
    "against"
    "all"
    "allow"
    "allows"
    "almost"
    "alone"
    "along"
    "already"
    "also"
    "although"
    "always"
    "am"
    "among"
    "amongst"
    "an"
    "and"
    "another"
    "any"
    "anybody"
    "anyhow"
    "anyone"
    "anything"
    "anyway"
    "anyways"
    "anywhere"
    "apart"
    "appear"
    "appreciate"
    "appropriate"
    "are"
    "around"
    "as"
    "aside"
    "ask"
    "asking"
    "associated"
    "at"
    "available"
    "away"
    "awfully"
    "b"
    "be"
    "became"
    "because"
    "become"
    "becomes"
    "becoming"
    "been"
    "before"
    "beforehand"
    "behind"
    "being"
    "believe"
    "below"
    "beside"
    "besides"
    "best"
    "better"
    "between"
    "beyond"
    "both"
    "brief"
    "but"
    "by"
    "c"
    "came"
    "can"
    "cannot"
    "cant"
    "cause"
    "causes"
    "certain"
    "certainly"
    "changes"
    "clearly"
    "co"
    "com"
    "come"
    "comes"
    "concerning"
    "consequently"
    "consider"
    "considering"
    "contain"
    "containing"
    "contains"
    "corresponding"
    "could"
    "course"
    "currently"
    "d"
    "definitely"
    "described"
    "despite"
    "did"
    "different"
    "do"
    "does"
    "doing"
    "done"
    "down"
    "downwards"
    "during"
    "e"
    "each"
    "edu"
    "eg"
    "eight"
    "either"
    "else"
    "elsewhere"
    "enough"
    "entirely"
    "especially"
    "et"
    "etc"
    "even"
    "ever"
    "every"
    "everybody"
    "everyone"
    "everything"
    "everywhere"
    "ex"
    "exactly"
    "example"
    "except"
    "f"
    "far"
    "few"
    "fifth"
    "first"
    "five"
    "followed"
    "following"
    "follows"
    "for"
    "former"
    "formerly"
    "forth"
    "four"
    "from"
    "further"
    "furthermore"
    "g"
    "get"
    "gets"
    "getting"
    "given"
    "gives"
    "go"
    "goes"
    "going"
    "gone"
    "got"
    "gotten"
    "greetings"
    "h"
    "had"
    "happens"
    "hardly"
    "has"
    "have"
    "having"
    "he"
    "hello"
    "help"
    "hence"
    "her"
    "here"
    "hereafter"
    "hereby"
    "herein"
    "hereupon"
    "hers"
    "herself"
    "hi"
    "him"
    "himself"
    "his"
    "hither"
    "hopefully"
    "how"
    "howbeit"
    "however"
    "i"
    "ie"
    "if"
    "ignored"
    "immediate"
    "in"
    "inasmuch"
    "inc"
    "indeed"
    "indicate"
    "indicated"
    "indicates"
    "inner"
    "insofar"
    "instead"
    "into"
    "inward"
    "is"
    "it"
    "its"
    "itself"
    "j"
    "just"
    "k"
    "keep"
    "keeps"
    "kept"
    "know"
    "knows"
    "known"
    "l"
    "last"
    "lately"
    "later"
    "latter"
    "latterly"
    "least"
    "less"
    "lest"
    "let"
    "like"
    "liked"
    "likely"
    "little"
    "look"
    "looking"
    "looks"
    "ltd"
    "m"
    "mainly"
    "many"
    "may"
    "maybe"
    "me"
    "mean"
    "meanwhile"
    "merely"
    "might"
    "more"
    "moreover"
    "most"
    "mostly"
    "much"
    "must"
    "my"
    "myself"
    "n"
    "name"
    "namely"
    "nd"
    "near"
    "nearly"
    "necessary"
    "need"
    "needs"
    "neither"
    "never"
    "nevertheless"
    "new"
    "next"
    "nine"
    "no"
    "nobody"
    "non"
    "none"
    "noone"
    "nor"
    "normally"
    "not"
    "nothing"
    "novel"
    "now"
    "nowhere"
    "o"
    "obviously"
    "of"
    "off"
    "often"
    "oh"
    "ok"
    "okay"
    "old"
    "on"
    "once"
    "one"
    "ones"
    "only"
    "onto"
    "or"
    "other"
    "others"
    "otherwise"
    "ought"
    "our"
    "ours"
    "ourselves"
    "out"
    "outside"
    "over"
    "overall"
    "own"
    "p"
    "particular"
    "particularly"
    "per"
    "perhaps"
    "placed"
    "please"
    "plus"
    "possible"
    "presumably"
    "probably"
    "provides"
    "q"
    "que"
    "quite"
    "qv"
    "r"
    "rather"
    "rd"
    "re"
    "really"
    "reasonably"
    "regarding"
    "regardless"
    "regards"
    "relatively"
    "respectively"
    "right"
    "s"
    "said"
    "same"
    "saw"
    "say"
    "saying"
    "says"
    "second"
    "secondly"
    "see"
    "seeing"
    "seem"
    "seemed"
    "seeming"
    "seems"
    "seen"
    "self"
    "selves"
    "sensible"
    "sent"
    "serious"
    "seriously"
    "seven"
    "several"
    "shall"
    "she"
    "should"
    "since"
    "six"
    "so"
    "some"
    "somebody"
    "somehow"
    "someone"
    "something"
    "sometime"
    "sometimes"
    "somewhat"
    "somewhere"
    "soon"
    "sorry"
    "specified"
    "specify"
    "specifying"
    "still"
    "sub"
    "such"
    "sup"
    "sure"
    "t"
    "take"
    "taken"
    "tell"
    "tends"
    "th"
    "than"
    "thank"
    "thanks"
    "thanx"
    "that"
    "thats"
    "the"
    "their"
    "theirs"
    "them"
    "themselves"
    "then"
    "thence"
    "there"
    "thereafter"
    "thereby"
    "therefore"
    "therein"
    "theres"
    "thereupon"
    "these"
    "they"
    "think"
    "third"
    "this"
    "thorough"
    "thoroughly"
    "those"
    "though"
    "three"
    "through"
    "throughout"
    "thru"
    "thus"
    "to"
    "together"
    "too"
    "took"
    "toward"
    "towards"
    "tried"
    "tries"
    "truly"
    "try"
    "trying"
    "twice"
    "two"
    "u"
    "un"
    "under"
    "unfortunately"
    "unless"
    "unlikely"
    "until"
    "unto"
    "up"
    "upon"
    "us"
    "use"
    "used"
    "useful"
    "uses"
    "using"
    "usually"
    "uucp"
    "v"
    "value"
    "various"
    "very"
    "via"
    "viz"
    "vs"
    "w"
    "want"
    "wants"
    "was"
    "way"
    "we"
    "welcome"
    "well"
    "went"
    "were"
    "what"
    "whatever"
    "when"
    "whence"
    "whenever"
    "where"
    "whereafter"
    "whereas"
    "whereby"
    "wherein"
    "whereupon"
    "wherever"
    "whether"
    "which"
    "while"
    "whither"
    "who"
    "whoever"
    "whole"
    "whom"
    "whose"
    "why"
    "will"
    "willing"
    "wish"
    "with"
    "within"
    "without"
    "wonder"
    "would"
    "would"
    "x"
    "y"
    "yes"
    "yet"
    "you"
    "your"
    "yours"
    "yourself"
    "yourselves"
    "z"
    "zero"
    ))

(defvar archer-stopwords 
  (make-vector (- (expt 2 (ceiling (log (length archer-stopwords-list) 2))) 1)
	       0))

(while archer-stopwords-list
  (intern (car archer-stopwords-list) archer-stopwords)
  (setq archer-stopwords-list (cdr archer-stopwords-list)))


(defun archer-stopword (word)
  (intern-soft word archer-stopwords))



;;; -------------- Commands ------------------

(defun archer-contact (host port)
  "Establish a connection with the Archer server at HOST on PORT."
  (interactive "sHost: \nnPort: ")
  (archer-disconnect)
  (setq archer-process 
	(open-network-stream archer-process-name 
			     archer-scratch-buffer host port))
  (cond ((and (archer-contacted) (archer-connection-allowed))
	 (message "Archer contacted")
	 (setq archer-server host)
	 (setq archer-port port)
	 (setq archer-prefix-length nil)
	 (setq archer-available-summary-fields nil)
	 (archer-send 
	  (if (= archer-max-hits 0) "hits" 
	    (format "hits 0 %d" (- archer-max-hits 1))))
	 t)
	(t (message "Failed to contact archer server")
	   (setq archer-server nil)
	   (setq archer-port nil)
	   (setq archer-process nil)
	   nil)))


(defun archer-query (query)
  "Send QUERY to current Archer server.  Prompts for a host and port
if not current connection exists."
  (interactive "sQuery: ")
  (setq archer-last-query query)
  (when (archer-send-query query)
    (set-buffer (get-buffer archer-scratch-buffer))
    (goto-char (point-min))
    (setq archer-hit-count (archer-read-hit-count))
    (let ((hits ())
	  (hit nil))
      (setq hit (archer-parse-hit))
      (while hit
	(setq hits (cons hit hits))
	(setq hit (archer-parse-hit)))
      (setq archer-hits 
	    (reverse 
	     (mapcar (lambda (h) (cons (cdr (assq 'id h)) h)) hits)))
      (cond (archer-hits 
	     (archer-show-hits) 
	     (message "%d hits" archer-hit-count))
	    (t (message "No hits"))))))


(defun archer-requery ()
  "Allows user to edit and re-send last query."
  (interactive)
  (let ((q (read-from-minibuffer "Query: " archer-last-query)))
    (when (> (length q) 0)
      (archer-query q))))


(defun archer-delete-summary-field ()
  "Delete a summary field currently used in displaying hits."
  (interactive)
  (if archer-summary-fields
      (let* ((collection 
	      (mapcar (lambda (f) (cons f t)) archer-summary-fields))
	     (field (completing-read "Delete field: " collection nil t)))
	(setq archer-summary-fields (delete field archer-summary-fields))
	(archer-show-hits))
    (message "No summary fields activated")))


(defun archer-insert-summary-field ()
  "Add a summary field to use in displaying hits."
  (interactive)
  (let ((collection 
	 (mapcar 
	  (lambda (f) (cons f t))
	  (archer-set-difference
	   archer-available-summary-fields archer-summary-fields))))
    (if collection
	(let ((field (completing-read "Add field: " collection nil t)))
	  (when (assoc field collection)
	    (setq archer-summary-fields 
		  (nconc archer-summary-fields (list field)))
	    (archer-show-hits)))
      (message "No summary fields available"))))


(defun archer-set-max-hits (number)
  "Set the number of hits to show in hit buffer."
  (interactive "nMax hits (0 = show all): ")
  (setq archer-max-hits number)
  (when (archer-contacted) 
    (archer-send (if (= number 0) "hits" (format "hits 0 %d" (- number 1)))))
  (when archer-last-query (archer-query archer-last-query)))


(defun archer-next-dump ()
  "Display the next matching document in the Archer dump buffer."
  (interactive)
  (set-buffer (get-buffer archer-hit-buffer))
  (archer-next-hit)
  (archer-find-hit)
  (set-buffer (get-buffer archer-dump-buffer)))

(defun archer-next-matcher ()
  "Go to the next term matching the query in the Archer dump buffer."
  (interactive)
  (when archer-matcher-positions
    (goto-char (archer-move-matcher-shown 1))))

(defun archer-previous-matcher ()
  "Go to the previous term matching the query in the Archer dump buffer."
  (interactive)
  (when archer-matcher-positions
    (goto-char (archer-move-matcher-shown -1))))


(defun archer-pop-to-hit-buffer ()
  "Pop to the Archer hit buffer."
  (interactive)
  (pop-to-buffer (get-buffer archer-hit-buffer)))


(defun archer-pop-to-dump-buffer ()
  "Pop to the Archer dump buffer."
  (interactive)
  (pop-to-buffer (get-buffer archer-dump-buffer)))


(defun archer-next-hit ()
  "Go down one line in the Archer hit buffer."
  (interactive)
  (let ((next-line-add-newlines nil)
	(goal-column 8))
    (next-line 1)))


(defun archer-previous-hit ()
  "Go up one line in the Archer hit buffer."
  (interactive)
  (let ((goal-column 8))
    (previous-line 1)))


(defun archer-find-hit ()
  "Display the document at the current line in the Archer hit buffer."
  (interactive)
  (end-of-line)
  (let ((p (point)))
    (beginning-of-line)
    (let ((result (re-search-forward "\\([0-9]+\\)" p t)))
      (if result 
	  (let ((id (match-string 1)))
	    (archer-dump id))
	(message "Can't find message id")))))


(defun archer-show-fields ()
  "Display a list of fields available for use in queries."
  (interactive)
  (archer-send "fields")
  (let ((fields 
	 (with-current-buffer archer-scratch-buffer 
	   (goto-char (point-min))
	   (archer-xml-strings "field"))))
    (if (not fields) (message "No fields defined")
      (let ((buf (current-buffer)))
	(pop-to-buffer (get-buffer-create archer-fields-buffer))
	(erase-buffer)
	(while fields
	  (insert (car fields) "\n")
	  (setq fields (cdr fields)))
	(pop-to-buffer buf)))))


(defun archer-query-region (from to)
  "Use region as query"
  (interactive "r")
  (let ((str (buffer-substring from to))
	(termobarray (make-vector 1023 0)))
    (with-current-buffer (get-buffer-create archer-scratch-buffer)
      (erase-buffer)
      (insert str)
      (downcase-region (point-min) (point-max))
      (goto-char (point-min))
      (perform-replace "[^a-zA-Z0-9]+" " " nil t nil)
      (goto-char (point-min))
      (while (re-search-forward "[a-z]+" nil t)
	(let ((word (match-string 0)))
	  (unless (archer-stopword word)
	    (let* ((sym (intern word termobarray))
		   (count (get sym 'count)))
	      (put sym 'count (if count (+ count 1) 1))))))
      (erase-buffer)
      (let ((found nil))
	(mapatoms (lambda (s) 
		    (insert (format "(%d)%s " (get s 'count) s))
		    (setq found t))
		  termobarray)
	(if found (archer-query (buffer-string))
	  (message "Nothing in region worth sending"))))))




;;; --------------- Archer connection management --------------


(defun archer-contacted ()
  "Do we have an open connection to an Archer server?"
  (let ((status (process-status archer-process-name)))
    (not (memq status '(nil closed)))))


(defun archer-disconnect ()
  "Disconnect from an open Archer connection."
  (when (archer-contacted)
    (delete-process archer-process)
    (with-current-buffer (get-buffer archer-scratch-buffer) (erase-buffer))))


(defun archer-connection-allowed ()
  "Will the Archer server let us talk to it?  If Archer requires
a password, get one from the user and send it."
  (archer-wait)
  (with-current-buffer (get-buffer archer-scratch-buffer)
    (goto-char (point-min))
    (or (not (archer-xml-region "archer-password")) (archer-password))))


(defun archer-password ()
  "Get a password from the user and send it.  Does Archer like it?"
  (let ((pass (archer-read-password)))
    (if (= (length pass) 0) nil
      (archer-send pass)
      (archer-contacted))))


(defun archer-read-password ()
  "Prompt the user for a password (wantonly plagiarized from ange-ftp)."
  (let ((pass nil)
	(c 0)
	(echo-keystrokes 0)
	(cursor-in-echo-area t))
    (while (progn (message "Password: %s" (make-string (length pass) ?*))
		  (setq c (read-char))
		  (and (/= c ?\r) (/= c ?\n) (/= c ?\e)))
      (cond ((= c ?\C-u) 
	     (setq pass ""))
	    ((and (/= c ?\b) (/= c ?\177)) 
	     (setq pass (concat pass (char-to-string c))))
	    ((> (length pass) 0) 
	     (setq pass (substring pass 0 -1)))))
    (message "")
    (message nil)
    pass))




;;; ------------------ Talking to Archer ----------------------


(defun archer-send-query (query)
  "Send QUERY to the current Archer process."
  (archer-send (concat "nquery " query)))


(defun archer-prepare-for-send ()
  "Get ready to send something to Archer."
  (with-current-buffer (get-buffer-create archer-scratch-buffer) 
    (erase-buffer)))


(defun archer-send (string)
  "Send STRING to Archer.  Return t if Archer's still with us, else nil."
  (let ((ok t))
    (unless (archer-contacted)
      (setq ok (call-interactively 'archer-contact)))
    (cond (ok 
	   (archer-prepare-for-send)
	   (process-send-string archer-process (concat string "\n")) 
	   (archer-wait)
	   t)
	  (t 
	   (message "No server contacted") 
	   nil))))


(defun archer-wait ()
  "Wait for Archer to stop talking."
  (with-current-buffer archer-scratch-buffer
    (goto-char (point-min))
    (while (not (search-forward "\n.\n" nil t))
      (beginning-of-line 0)
      (accept-process-output archer-process 0 100))))




;;; ------------------- Understanding Archer ------------------

; ((id . "id") 
;  (name . "name") 
;  (score . "score") 
;  (terms ("term" "term"))
;  (annotations (name . value) ...)
(defun archer-parse-hit ()
  "Parse and return a single Archer hit, advancing point past it.
Return nil if no hits are visible from current point."
  (let ((region (archer-xml-region "hit")))
    (and region
	 (let ((hit ()))
	   (goto-char (car region))
	   (let* ((a (archer-xml-region "annotation" (cdr region)))
		  (stop (if a (car a) (cdr region))))
	     (mapcar (lambda (tag) 
		       (let ((strings 
			      (archer-xml-strings (symbol-name tag) stop)))
			 (when strings 
			   (setq hit (cons (cons tag (car strings)) hit)))))
		   '(id name score))
	     (let ((terms (archer-xml-strings "term" stop)))
	       (when terms (setq hit (cons (cons 'terms terms) hit))))
	     (when a
	       (goto-char (car a))
	       (let ((names (archer-xml-strings "name" (cdr region)))
		     (values (archer-xml-strings "value" (cdr region)))
		     (annotations ()))
		 (while names
		   (setq annotations 
			 (cons (cons (car names) (car values)) annotations))
		   (setq names (cdr names))
		   (setq values (cdr values)))
		 (when annotations
		   (setq hit (cons (cons 'annotations annotations) hit))
		   (unless archer-available-summary-fields
		     (setq archer-available-summary-fields 
			   (mapcar 'car annotations)))))))
	   hit))))


(defun archer-read-hit-count ()
  "Read the number of matching documents from the Archer scratch buffer."
  (let ((region (archer-xml-region "count")))
    (unless region (error "Can't read count of matching documents"))
    (string-to-number (buffer-substring (car region) (cdr region)))))


(defun archer-xml-strings (tag &optional limit)
  "Return as a list all strings in fields delimited by TAG up to 
optional LIMIT."
  (save-excursion
    (let ((result ())
	  (region (archer-xml-region tag limit)))
      (while region
	(setq result
	      (cons (buffer-substring (car region) (cdr region)) result))
	(goto-char (cdr region))
	(setq region (archer-xml-region tag limit)))
      result)))


(defun archer-xml-region (&optional tag limit)
  "Return the next xml region (in the form of (from-point . to-point))
visible from the current point.  If TAG is specified, concentrate on
the indicated xml field.  Only search up to LIMIT, if specified."
  (save-excursion
    (let ((regexp
	   (if tag (concat "<\\(" tag "\\)>") "<\\([a-zA-Z]+\\)>")))
      (let ((from (re-search-forward regexp limit t)))
	(and from
	     (let* ((tag (match-string 1))
		    (pat (concat "</" tag ">"))
		    (to (search-forward pat limit t)))
	       (and to (cons from (- to (length pat))))))))))


(defun archer-matching-text ()
  "Return a list of regions corresponding to matching terms."
  (archer-xml-region "match"))




;;; ----------------- Displaying results ----------------------


(defun archer-show-hits ()
  "Show matching documents, one per line, in the hits buffer."
  (switch-to-buffer (get-buffer-create archer-hit-buffer))
  (erase-buffer)
  (mapcar 'archer-insert-hit archer-hits)
  (use-local-map archer-hit-map)
  (goto-char (point-min)))


(defun archer-insert-hit (hit)
  "Insert a representation of HIT at point in the hits buffer."
  (let* ((id (cdr (assq 'id hit)))
	 (idlen (length id))
	 (score (archer-truncate-score (cdr (assoc 'score hit))))
	 (from nil)
	 (to nil))
    (setq from (point))
    (insert (format "%-8s" id))
    (setq to (point))
    (put-text-property from to 'invisible t)
    (insert (format "%-6s" score))
    (archer-insert-summary hit (- (window-width) 6))
    (insert "\n")))


(defun archer-insert-summary (hit columns)
  "Construct a summary from HIT, at most COLUMNS long, and insert
into the hits buffer."
  (let* ((ann (cdr (assq 'annotations hit)))
	 (sumfields 
	  (if (listp archer-summary-fields) archer-summary-fields
	    (list archer-summary-fields)))
	 (sumsize 
	  (if sumfields (- (truncate (/ columns (length sumfields))) 2) 0)))
    (if (and sumfields ann)
	(while sumfields
	  (archer-insert-field-summary ann (car sumfields) sumsize)
	  (when (cdr sumfields) (insert "  "))
	  (setq sumfields (cdr sumfields)))
      (insert (cdr (assq 'name hit))))))


(defun archer-insert-field-summary (annotations field columns)
  "From the ANNOTATIONS list find the string corresponding to FIELD
and truncate it or widen it so it occupies COLUMNS columns.  Finally,
insert it into the hits buffer."
  (let ((val (cdr (assoc field annotations))))
    (setq val
	  (if (= (length val) 0) ""
	    (with-temp-buffer
	      (insert val)
	      (archer-replace-entities)
	      (buffer-string))))
    (insert (if (<= (length val) columns) 
		(format (format "%%-%ds" columns) val)
	      (substring val 0 columns)))))


(defun archer-truncate-score (score)
  "Shorten the string SCORE so it fits into available space.  Return
the modified string."
  (let ((len (length score))
	(i 0))
    (while (and (< i len) (/= (elt score i) ?.))
      (setq i (+ i 1)))
    (if (= i len) score
      (let ((end (+ i 3)))
	(when (> end len) (setq end len))
	(substring score 0 end)))))


(defun archer-dump (id)
  "Present the document corresponding to the string ID in the dump buffer."
  (archer-send (concat "ndump " id))
  (set-buffer (get-buffer archer-scratch-buffer))
  (goto-char (point-min))
  (let ((doc (archer-xml-region "document")))
    (if doc
	(let ((matcher nil))
	  (pop-to-buffer (get-buffer-create archer-dump-buffer))
	  (erase-buffer)
	  (insert-buffer-substring archer-scratch-buffer (car doc) (cdr doc))
	  (setq archer-matcher-positions (archer-highlight-matchers))
	  (setq archer-matcher-shown nil)
	  (archer-replace-entities)
	  (goto-char (point-min))
	  (use-local-map archer-dump-map))
      (message "Couldn't parse Archer result"))))


(defun archer-highlight-matcher (matcher)
  "Highlight the region of text specified by MATCHER.  Delete the XML tags."
  (put-text-property (car matcher) (cdr matcher)
		     'face 'archer-highlight-face)
  (let* ((mlen (length "<match>"))
	 (emlen (+ mlen 1)))
    (delete-region (- (car matcher) mlen) (car matcher))
    (delete-region (- (cdr matcher) mlen) (- (+ (cdr matcher) emlen) mlen))))


(defun archer-highlight-matchers ()
  "Highlight all matching terms in the dump buffer."
  (let (lines)
    (goto-char (point-min))
    (setq matcher (archer-matching-text))
    (while matcher
      (goto-char (car matcher))
      (let ((m (point-marker)))
	(setq lines (cons m lines)))
      (archer-highlight-matcher matcher)
      (setq matcher (archer-matching-text)))
    (reverse lines)))


(defvar archer-entities 
  '("&amp;" "&AMP;" "&quot;" "&QUOT;" "&gt;" "&GT;" 
    "&lt;" "&LT;" "&apos;" "&APOS;")
  "XML escape codes used by Archer...")
(defvar archer-replacements '("&" "&" "\"" "\"" ">" ">" "<" "<" "'" "'")
  "...and their appropriate replacements")


(defun archer-replace-entities ()
  "Replace all XML-escaped text by its original form."
  (let ((entities archer-entities)
	(replacements archer-replacements))
    (while entities
      (let ((e (car entities))
	    (r (car replacements)))
	(goto-char (point-min))
	(while (search-forward e nil t) (replace-match r nil t))
	(setq entities (cdr entities))
	(setq replacements (cdr replacements))))))




;;; ------------------ Keymaps ----------------------


(defun archer-make-hit-map ()
  "Construct the keymap for the hit buffer."
  (setq archer-hit-map (make-keymap))
  (suppress-keymap archer-hit-map)

  (define-key archer-hit-map "" 'scroll-down)
  (define-key archer-hit-map " " 'scroll-up)
  (define-key archer-hit-map "c" 'archer-contact)
  (define-key archer-hit-map "d" 'archer-delete-summary-field)
  (define-key archer-hit-map "f" 'archer-find-hit)
  (define-key archer-hit-map "F" 'archer-show-fields)
  (define-key archer-hit-map "i" 'archer-insert-summary-field)
  (define-key archer-hit-map "m" 'archer-set-max-hits)
  (define-key archer-hit-map "n" 'archer-next-hit)
  (define-key archer-hit-map "o" 'archer-pop-to-dump-buffer)
  (define-key archer-hit-map "p" 'archer-previous-hit)
  (define-key archer-hit-map "q" 'archer-query)
  (define-key archer-hit-map "r" 'archer-requery)
)

(unless archer-hit-map (archer-make-hit-map))

(defun archer-make-dump-map ()
  "Construct the keymap for the dump buffer."
  (setq archer-dump-map (make-keymap))
  (suppress-keymap archer-dump-map)

  (define-key archer-dump-map "" 'scroll-down)
  (define-key archer-dump-map " " 'scroll-up)
  (define-key archer-dump-map "c" 'archer-contact)
  (define-key archer-dump-map "d" 'archer-delete-summary-field)
  (define-key archer-dump-map "F" 'archer-show-fields)
  (define-key archer-dump-map "i" 'archer-insert-summary-field)
  (define-key archer-dump-map "m" 'archer-set-max-hits)
  (define-key archer-dump-map "n" 'archer-next-matcher)
  (define-key archer-dump-map "o" 'archer-pop-to-hit-buffer)
  (define-key archer-dump-map "p" 'archer-previous-matcher)
  (define-key archer-dump-map "q" 'archer-query)
  (define-key archer-dump-map "v" 'archer-next-dump)
  (define-key archer-dump-map "r" 'archer-requery)
)

(unless archer-dump-map (archer-make-dump-map))





;;; -------------------- Miscellaneous ---------------------------


(defun archer-set-difference (list1 list2)
  "Return all elements in LIST1 not in LIST2."
  (cond ((null list1) nil)
	((member (car list1) list2) (archer-set-difference (cdr list1) list2))
	(t (cons (car list1) (archer-set-difference (cdr list1) list2)))))


(defun archer-move-matcher-shown (nth)
  "Return the NTH previous/next (depending on sign) matching-term marker."
  (setq archer-matcher-shown 
	(if archer-matcher-shown (+ archer-matcher-shown nth)
	  (if (> nth 0) 0 -1)))
  (when (< archer-matcher-shown 0)
    (beep)
    (message "No previous matches")
    (setq archer-matcher-shown 0))
  (when (>= archer-matcher-shown (length archer-matcher-positions))
    (beep)
    (message "No further matches")
    (setq archer-matcher-shown (- (length archer-matcher-positions) 1)))
  (marker-position (elt archer-matcher-positions archer-matcher-shown)))


(defun archer-compute-prefix-length ()
  "Calculate the length of the longest prefix common to all document
names (currently not used)."
  (let* ((first (car archer-hits))
	 (str (cdr (assoc "name" first)))
	 (cur (or archer-prefix-length (length str)))
	 (pre (substring str 0 cur)) 
	 (rest (cdr archer-hits)))
    (while rest
      (let ((next (car rest))
	    (str2 (cdr (assoc "name" next)))
	    (len (length str2)))
	(when (< len cur) 
	  (setq cur len)
	  (setq pre (substring str 0 cur)))
	(while (and (> cur 0) (not (equal pre (substring str2 0 cur))))
	  (setq cur (- cur 1))
	  (setq pre (substring str 0 cur)))
	(setq rest (cdr rest))))
    (setq archer-prefix-length cur)))


