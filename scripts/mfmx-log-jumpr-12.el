(require 'cl-lib) ;; for cl-block and cl-return-from

(defun advance-to-next-mark-here ()
  (interactive)
  (jump-to-next-mfmx-mark nil nil))

(defun advance-to-next-mark ()
  (interactive)
  (let* ((curbuf (current-buffer))
         (databuf (show-latest-data)))
    (switch-to-buffer databuf)
    (jump-to-next-mfmx-mark nil nil)
    (switch-to-buffer curbuf)))

(defun retreat-to-prev-mark-here ()
  (interactive)
  (jump-to-next-mfmx-mark t nil))

(defun retreat-to-prev-mark ()
  (interactive)
  (let* ((curbuf (current-buffer))
         (databuf (show-latest-data)))
    (switch-to-buffer databuf)
    (jump-to-next-mfmx-mark t nil)
    (switch-to-buffer curbuf)))

(defun advance-to-next-similar-mark-here ()
  (interactive)
  (let* ((cur-mark (mfmx-mark-at-point))
         (fields (and cur-mark (mfmx-mark-fields cur-mark))))
    (unless fields
      (error "No valid mark at point"))
    (jump-to-next-mfmx-mark nil (mfmx-similar-mark-regexp fields))))

(defun advance-to-next-similar-mark ()
  (interactive)
  (let* ((curbuf (current-buffer))
         (databuf (show-latest-data)))
    (switch-to-buffer databuf)
    (let* ((cur-mark (mfmx-mark-at-point))
           (fields (and cur-mark (mfmx-mark-fields cur-mark))))
      (unless fields
        (error "No valid mark at point"))
      (jump-to-next-mfmx-mark nil (mfmx-similar-mark-regexp fields)))
    (switch-to-buffer curbuf)))

(defun retreat-to-prev-similar-mark-here ()
  (interactive)
  (let* ((cur-mark (mfmx-mark-at-point))
         (fields (and cur-mark (mfmx-mark-fields cur-mark))))
    (unless fields
      (error "No valid mark at point"))
    (jump-to-next-mfmx-mark t (mfmx-similar-mark-regexp fields))))

(defun retreat-to-prev-similar-mark ()
  (interactive)
  (let* ((curbuf (current-buffer))
         (databuf (show-latest-data)))
    (switch-to-buffer databuf)
    (let* ((cur-mark (mfmx-mark-at-point))
           (fields (and cur-mark (mfmx-mark-fields cur-mark))))
      (unless fields
        (error "No valid mark at point"))
      (jump-to-next-mfmx-mark t (mfmx-similar-mark-regexp fields)))
    (switch-to-buffer curbuf)))

(defun switch-to-most-recent-matching-buffer (regexp)
  (interactive
   (list (read-from-minibuffer "Buffer name (regexp): " "\\.mfmk$")))
  (let ((matching-buffer nil))
    (cl-dolist (buffer (buffer-list))
      (when (string-match-p regexp (buffer-name buffer))
        (setq matching-buffer buffer)
        (cl-return)))
    (if (not matching-buffer)
        (error "No match for %s" regexp)
      (switch-to-buffer matching-buffer))
    matching-buffer))

(defun display-most-recent-matching-buffer (regexp)
  (interactive "sBuffer name (regexp): ")
  (let ((matching-buffer nil))
    (cl-dolist (buffer (buffer-list))
      (when (string-match-p regexp (buffer-name buffer))
        (setq matching-buffer buffer)
        (cl-return)))
    (if (not matching-buffer)
        (error "No match for %s" regexp)
      (display-buffer matching-buffer))
    matching-buffer))

(defun show-latest-data ()
  (interactive)
  (let ((buf (display-most-recent-matching-buffer "all.mfmk")))
    (save-excursion
      (switch-to-buffer buf)
      (hl-line-mode nil))
    buf))

(defun search-and-mark-braces (bkwd filter)
  "Search for a mark and set point and mark at the match boundaries.
If FILTER is non-nil, use it as the regexp to search for.
Otherwise, search for any {...} construct."
  (let ((re (or filter "{\\([^{}]*\\)}")))
    (cond
     ((and (not bkwd) (re-search-forward re nil t))
      (set-mark (match-beginning 0)) ; Set mark at the beginning of the match
      (goto-char (match-end 0))     ; Move point to the end of the match
      (match-string 0))             ; return whole string
     ((and bkwd (re-search-backward re nil t))
      (set-mark (match-end 0))      ; Set mark at end of match
      (goto-char (match-beginning 0)) ; Move point to start of match
      (match-string 0))             ; return whole string
     (t (error "NO MARK FOUND")))))

(defun mfmx-mark-at-point ()
  "Return the mark string at or before point, or nil."
  (save-excursion
    (save-match-data
      (let ((orig (point)))
        (goto-char (line-beginning-position))
        (if (re-search-forward "{\\([^{}]*\\)}" (line-end-position) t)
            (match-string 0)
          (goto-char orig)
          (when (re-search-backward "{\\([^{}]*\\)}" nil t)
            (match-string 0)))))))

(defun mfmx-mark-fields (mark-string)
  "Parse MARK-STRING into an alist of fields.
Format: {locId:lineNum time device xcoord,ycoord thread text...}
Returns nil if the string doesn't parse."
  (let ((s (string-trim mark-string)))
    (when (string-match "^{\\([^{}]*\\)}$" s)
      (let ((inner (match-string 1 s)))
        (when (string-match
               "^\\([0-9]+:[0-9]+\\) \\([^ ]+\\) \\([^ ]+\\) \\([^ ]+\\) \\([^ ]+\\) \\(.*\\)$"
               inner)
          (list (cons 'loc    (match-string 1 inner))
                (cons 'time   (match-string 2 inner))
                (cons 'device (match-string 3 inner))
                (cons 'coords (match-string 4 inner))
                (cons 'thread (match-string 5 inner))
                (cons 'text   (match-string 6 inner))))))))

(defun mfmx-similar-mark-regexp (fields)
  "Build a regexp matching marks with same device/coords/thread as FIELDS.
Wildcards locId:lineNum and time."
  (let ((device (cdr (assoc 'device fields)))
        (coords (cdr (assoc 'coords fields)))
        (thread (cdr (assoc 'thread fields))))
    (concat "{[0-9]+:[0-9]+ \\S-+ "
            (regexp-quote device) " "
            (regexp-quote coords) " "
            (regexp-quote thread)
            " [^{}]*}")))

(defun get-braced-plain-text (dir filter)
  (cl-block body
    (let ((current-file-path (buffer-file-name))
          (next-match (search-and-mark-braces dir filter)))
      ;; 1. Search forward for '{', quitting if none is found
      (unless next-match
        (message "No opening brace '{' found.")
        (ding)
        (cl-return-from body nil))

      (setq plain-text (substring-no-properties next-match))
      ;; Point is already left right after the close brace by search-forward
      (list current-file-path plain-text))))

(defun run-shell-command-and-split-result (shell-command filepath plain-text)
  (cl-block body
    (let* (shell-output cmd)
      (unless plain-text
        (cl-return-from body nil))
      (setq cmd (format "%s %s %s"
                        shell-command
                        (shell-quote-argument filepath)
                        (shell-quote-argument plain-text)))
      (setq shell-output
            (shell-command-to-string cmd))
      (if (string-equal shell-output "")
          (list "" "0")
          (split-string shell-output ":")))))

(defun find-file-at-line (path lineno)
  (let* ((target-buffer (find-file-noselect path))
         (log-win (selected-window))
         (target-window (next-window log-win)))
    (cond
     (target-window
      (save-selected-window
        (select-window target-window)
        (find-file path)
        (with-current-buffer (window-buffer target-window)
          ;; flush old overlays
          (dolist (ov (overlays-in (point-min) (point-max)))
            (when (overlay-get ov 'mfmx-hl-tag)
              (delete-overlay ov)))
          (goto-char (point-min))
          (forward-line (1- lineno))
          (let ((overlay (make-overlay (line-beginning-position) (line-end-position))))
            (overlay-put overlay 'face 'mfmx-hl-line-face)
            (overlay-put overlay 'mfmx-hl-tag t)
            (overlay-put overlay 'priority 91)
            (overlay-put overlay 'evaporate t)))
        (recenter nil t))
      (select-window log-win))
     (t (print (list path target-buffer target-window))))))

(defun visit-file-and-position-from-mfmx-mark (shell-command file-path plain-text)
  (let* ((args (run-shell-command-and-split-result
                shell-command file-path plain-text)))
    (if (not args)
        (progn (message "Parse failed on '%s'" plain-text) nil)
      (message "GOTZ '%s' <- '%s' from %s" args plain-text shell-command)
      (find-file-at-line (nth 0 args) (string-to-number (nth 1 args))))))

(defun jump-to-next-mfmx-mark (dir filter)
  (let* ((info (get-braced-plain-text dir filter))
         (current-file-path (nth 0 info))
         (plain-text (nth 1 info))
         (shell-command "/data/ackley/PART4/code/D/MFMx-spikes/scripts/parsemark.pl"))
    (visit-file-and-position-from-mfmx-mark shell-command current-file-path plain-text)))

(unless (featurep 'mfmx-log-jumpr-mode)
  (defface mfmx-hl-line-face
    '((t :background "color-20"
         :foreground "brightwhite"))
    "Custom face for coloring the highlighted line"
    :group 'hl-line)
  (defvar mfmx-log-jumpr-mode-map (make-sparse-keymap))
  (define-minor-mode mfmx-log-jumpr-mode
    "Minor mode for navigating all.mfmk log file marks"
    nil
    :lighter " logMFMx"
    mfmx-log-jumpr-mode-map
    (setq-local hl-line-face 'mfmx-hl-line-face)
    (if mfmx-log-jumpr-mode
        (read-only-mode 1)
      (read-only-mode -1)))
  (define-key mfmx-log-jumpr-mode-map (kbd "<down>") 'advance-to-next-mark)
  (define-key mfmx-log-jumpr-mode-map (kbd "<up>") 'retreat-to-prev-mark)
  (define-key mfmx-log-jumpr-mode-map (kbd "C-<down>") 'advance-to-next-mark-here)
  (define-key mfmx-log-jumpr-mode-map (kbd "C-<up>") 'retreat-to-prev-mark-here)
  (define-key mfmx-log-jumpr-mode-map (kbd "M-<down>") 'advance-to-next-similar-mark)
  (define-key mfmx-log-jumpr-mode-map (kbd "M-<up>") 'retreat-to-prev-similar-mark)
  (define-key mfmx-log-jumpr-mode-map (kbd "C-M-<down>") 'advance-to-next-similar-mark-here)
  (define-key mfmx-log-jumpr-mode-map (kbd "C-M-<up>") 'retreat-to-prev-similar-mark-here)
  (define-key mfmx-log-jumpr-mode-map (kbd "RET") 'show-latest-data)
  (define-key mfmx-log-jumpr-mode-map (kbd "r") 'revert-buffer)
  (provide 'mfmx-log-jumpr-mode))

(defun enable-mfmx-mode-for-ext-mfmk ()
  (when (and buffer-file-name
             (string-match "\\.mfmk\\'" buffer-file-name))
    (message "HIT %s" buffer-file-name)
    (mfmx-log-jumpr-mode 1)))

;;;(remove-hook 'find-file-hook #'enable-mfmx-mode-for-all-txt)
(add-hook 'find-file-hook #'enable-mfmx-mode-for-ext-mfmk)
